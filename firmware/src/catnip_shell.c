/* catnip_shell.c - see catnip_shell.h. */
#include "catnip_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catnip_manifest.h"
#include "catnip_ui.h"

struct catnip_shell {
    catnip_rt *rt;
    catnip_sched *sched;
    char apps_root[256];
    catnip_app_entry apps[CATNIP_SHELL_MAX_APPS];
    int n_apps;
    int state;
    int running; /* index of the running app, or -1 */
};

catnip_shell *catnip_shell_new(catnip_rt *rt, const char *apps_root,
                               catnip_now_fn now, catnip_pump_fn pump, void *ud)
{
    if (!rt || !apps_root) return NULL;
    catnip_shell *s = (catnip_shell *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->rt = rt;
    s->sched = catnip_sched_new(rt, now, pump, ud);
    if (!s->sched) { free(s); return NULL; }
    catnip_ui_open(rt); /* apps get ui.* */
    snprintf(s->apps_root, sizeof(s->apps_root), "%s", apps_root);
    s->state = CATNIP_SHELL_MENU;
    s->running = -1;
    catnip_shell_refresh(s);
    return s;
}

int catnip_shell_refresh(catnip_shell *s)
{
    if (!s) return 0;
    int n = catnip_loader_discover(s->apps_root, s->apps, CATNIP_SHELL_MAX_APPS);
    s->n_apps = (n < 0) ? 0 : n;
    return s->n_apps;
}

int catnip_shell_count(const catnip_shell *s) { return s ? s->n_apps : 0; }

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
    catnip_manifest m;
    char *code = NULL;
    int rc = catnip_loader_open(s->apps[index].dir, &m, &code, errbuf, errlen);
    if (rc != 0) return rc; /* incompatible or invalid: stay in MENU */

    rc = catnip_sched_start(s->sched, code, m.id);
    free(code);
    if (rc != 0) {
        if (errbuf && errlen) snprintf(errbuf, errlen, "app failed to load");
        return rc;
    }
    s->state = CATNIP_SHELL_RUNNING;
    s->running = index;
    return 0;
}

int catnip_shell_launch_id(catnip_shell *s, const char *id, char *errbuf,
                           size_t errlen)
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

    int st = catnip_sched_step(s->sched);
    if (st == CATNIP_DONE || st == CATNIP_ERROR) {
        s->state = CATNIP_SHELL_MENU; /* app finished or faulted: back to menu */
        s->running = -1;
    }
    return s->state;
}

void catnip_shell_exit(catnip_shell *s)
{
    if (!s) return;
    s->state = CATNIP_SHELL_MENU;
    s->running = -1;
}

void catnip_shell_free(catnip_shell *s)
{
    if (!s) return;
    catnip_sched_free(s->sched);
    free(s);
}
