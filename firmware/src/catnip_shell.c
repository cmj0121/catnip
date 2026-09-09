/* catnip_shell.c - see catnip_shell.h. */
#include "catnip_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catnip_manifest.h"
#include "catnip_render.h"
#include "catnip_ui.h"

struct catnip_shell {
    catnip_rt *rt;
    catnip_sched *sched;
    const catnip_render_backend *be; /* only for teardown; see the header */
    char apps_root[256];
    catnip_app_entry apps[CATNIP_SHELL_MAX_APPS];
    int n_apps;
    int state;
    int running;  /* index of the running app, or -1 */
    int resident; /* the app's setup is done but its UI lives on its handlers */
    int bare;     /* the running app asked for the whole panel; see the header */
    int hints;    /* the running app takes the control hint; see the header */
};

catnip_shell *catnip_shell_new(catnip_rt *rt, const char *apps_root, catnip_now_fn now,
                               catnip_pump_fn pump, void *ud)
{
    if (!rt || !apps_root) return NULL;
    catnip_shell *s = (catnip_shell *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->rt = rt;
    s->sched = catnip_sched_new(rt, now, pump, ud);
    if (!s->sched) {
        free(s);
        return NULL;
    }
    catnip_ui_open(rt); /* apps get ui.* */
    /* Handlers run on their own one-shot coroutine with the watchdog armed.
     * Installing it here is what makes that true for every app the shell
     * launches, rather than for whichever caller remembered to. */
    catnip_render_set_dispatch(rt, catnip_sched_dispatch, s->sched);
    snprintf(s->apps_root, sizeof(s->apps_root), "%s", apps_root);
    s->state = CATNIP_SHELL_MENU;
    s->running = -1;
    catnip_shell_refresh(s);
    return s;
}

int catnip_shell_refresh(catnip_shell *s)
{
    if (!s) return 0;
    /* Built in first, and the card after: the ones that are always there come
     * before the ones that may not be, so the order a user learns does not
     * change when a card is taken out. */
    int n = catnip_loader_builtin(s->apps, CATNIP_SHELL_MAX_APPS);
    int m = catnip_loader_discover(s->apps_root, s->apps + n, CATNIP_SHELL_MAX_APPS - n);
    s->n_apps = n + ((m < 0) ? 0 : m);
    return s->n_apps;
}

int catnip_shell_count(const catnip_shell *s)
{
    return s ? s->n_apps : 0;
}

const catnip_app_entry *catnip_shell_app(const catnip_shell *s, int index)
{
    if (!s || index < 0 || index >= s->n_apps) return NULL;
    return &s->apps[index];
}

int catnip_shell_state(const catnip_shell *s)
{
    return s ? s->state : CATNIP_SHELL_MENU;
}

int catnip_shell_launch(catnip_shell *s, int index, char *errbuf, size_t errlen)
{
    if (!s) return -1;
    if (index < 0 || index >= s->n_apps) {
        if (errbuf && errlen) snprintf(errbuf, errlen, "no such app");
        return -1;
    }
    /* Whatever the last app left on the glass goes before the next one starts.
     * The Lua heap is accounted, so a tree that survives here is charged to the
     * app about to run, and the symptom would be that app failing to allocate -
     * about as far from the cause as a fault can get. */
    catnip_render_reset(s->rt, s->be);

    catnip_manifest m;
    char *code = NULL;
    int rc = catnip_loader_open(s->apps[index].dir, &m, &code, errbuf, errlen);
    if (rc != 0) return rc; /* incompatible or invalid: stay in MENU */

    /* Remembered here rather than acted on: what a bare frame looks like is the
     * device's business, and the caller asks for this the moment the launch
     * succeeds - before the app's first screen is built, which is what matters.
     */
    s->bare = m.bare;
    s->hints = m.hints;

    rc = catnip_sched_start(s->sched, code, m.id);
    free(code);
    if (rc != 0) {
        if (errbuf && errlen) snprintf(errbuf, errlen, "app failed to load");
        return rc;
    }
    s->state = CATNIP_SHELL_RUNNING;
    s->resident = 0;
    s->running = index;
    return 0;
}

int catnip_shell_launch_id(catnip_shell *s, const char *id, char *errbuf, size_t errlen)
{
    if (!s || !id) return -1;
    for (int i = 0; i < s->n_apps; i++) {
        if (strcmp(s->apps[i].id, id) == 0)
            return catnip_shell_launch(s, i, errbuf, errlen);
    }
    if (errbuf && errlen) snprintf(errbuf, errlen, "no app with id '%s'", id);
    return -1;
}

int catnip_shell_step(catnip_shell *s)
{
    if (!s) return CATNIP_SHELL_MENU;
    if (s->state != CATNIP_SHELL_RUNNING) return s->state;

    /* An app that has finished says so, and is taken at its word.
     *
     * Before the resident check, not after it. A resident app is precisely the
     * one that would ask: its main chunk has returned and it lives on through
     * its handlers, so a handler is the only place left for it to say it is
     * done. Checking after meant the request was read for exactly the apps that
     * could never make it, and A on the clock's setter did nothing at all. */
    if (catnip_sched_take_exit(s->sched)) {
        catnip_shell_exit(s);
        return s->state;
    }

    /* Once an app is resident its main coroutine is gone; there is nothing to
     * step. It stays on screen and its handlers run in the render drain, until
     * B backs out of it or it faults some other way. */
    if (s->resident) return s->state;

    int st = catnip_sched_step(s->sched);
    if (st == CATNIP_ERROR) {
        /* A fault ends the app. The scheduler drops the coroutine, but that
         * does not collect the widget tree - it is rooted in the ui module and
         * in the renderer's strong table, and the handlers hold what the app
         * captured - so teardown is said out loud. */
        catnip_render_reset(s->rt, s->be);
        s->state = CATNIP_SHELL_MENU;
        s->running = -1;
    } else if (st == CATNIP_DONE) {
        /* The main chunk returned. An event-driven app builds its screen and
         * returns, meaning to live on through its handlers, which run in the
         * drain and need no live coroutine (#30/#31); a script with no screen
         * has genuinely finished. The screen is what tells them apart. */
        if (catnip_ui_has_screen(s->rt)) {
            s->resident = 1; /* keep it up; only B or a fault ends it now */
        } else {
            catnip_render_reset(s->rt, s->be);
            s->state = CATNIP_SHELL_MENU;
            s->running = -1;
        }
    }
    return s->state;
}

int catnip_shell_bare(const catnip_shell *s)
{
    return (s && s->state == CATNIP_SHELL_RUNNING) ? s->bare : 0;
}

int catnip_shell_hints(const catnip_shell *s)
{
    /* The launcher's own screens always take the hint: it is the one place
     * where a direction meaning nothing is most likely and least expected. */
    if (!s || s->state != CATNIP_SHELL_RUNNING) return 1;
    return s->hints;
}

const char *catnip_shell_title(const catnip_shell *s)
{
    if (!s || s->state != CATNIP_SHELL_RUNNING) return "";
    const char *t = catnip_ui_title(s->rt);
    if (t && t[0]) return t;
    if (s->running >= 0 && s->running < s->n_apps) return s->apps[s->running].name;
    return "";
}

int catnip_shell_back(catnip_shell *s)
{
    if (!s) return CATNIP_SHELL_MENU;
    if (s->state != CATNIP_SHELL_RUNNING) return s->state;

    /* Read it either way: an unread claim from a back the app did handle would
     * otherwise still be standing when the next one arrives. */
    int claimed = catnip_render_take_claim(s->rt);
    if (claimed) return s->state; /* the app climbed a level; it stays */

    /* Nothing claimed it. A screen pushed above the app's root is the
     * platform's to close - that is what makes a confirmation cancellable by B
     * with no on_back anywhere in the app. */
    if (catnip_ui_depth(s->rt) > 1) {
        catnip_ui_pop(s->rt);
        return s->state;
    }

    catnip_shell_exit(s);
    return s->state;
}

int catnip_shell_home(catnip_shell *s)
{
    if (!s) return CATNIP_SHELL_MENU;
    /* Any claim in flight belonged to a question that no longer matters, and
     * leaving it standing would let it answer the next app's first B. */
    (void)catnip_render_take_claim(s->rt);
    if (s->state == CATNIP_SHELL_RUNNING) catnip_shell_exit(s);
    return s->state;
}

void catnip_shell_set_backend(catnip_shell *s, const catnip_render_backend *be)
{
    if (!s) return;
    s->be = be;
}

void catnip_shell_exit(catnip_shell *s)
{
    if (!s) return;
    catnip_render_reset(s->rt, s->be);
    s->state = CATNIP_SHELL_MENU;
    s->running = -1;
    s->resident = 0;
}

void catnip_shell_free(catnip_shell *s)
{
    if (!s) return;
    catnip_sched_free(s->sched);
    free(s);
}
