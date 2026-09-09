/* catnip_pages.c - see catnip_pages.h. */
#include "catnip_pages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catnip_render.h"
#include "device/rtc_time.h"
#include "device/ui_input.h"

struct catnip_pages {
    catnip_rt *rt;
    catnip_shell *shell;
    catnip_config *cfg;
    catnip_pages_env env;

    catnip_menu *menu;
    catnip_settings *settings;
    catnip_device_info *info;

    catnip_page page;
    /* Whether anything on the preference page has been stepped since it was
     * opened, and what it looked like before the first step. Two fields for one
     * promise: nothing is written unless something changed, and B puts back
     * exactly what was there. */
    bool unsaved;
    catnip_config was;
    /* What the ring's clock cell was last told, so a pass that changes nothing
     * costs nothing. */
    char shown_time[16];
};

static void call_title(catnip_pages *p, const char *t)
{
    if (p->env.title) p->env.title(p->env.ud, t);
}

catnip_pages *catnip_pages_new(catnip_rt *rt, catnip_shell *shell, catnip_config *cfg,
                               const catnip_pages_env *env)
{
    catnip_pages *p;

    if (!rt || !cfg) return NULL;
    p = (catnip_pages *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->rt = rt;
    p->shell = shell;
    p->cfg = cfg;
    if (env) p->env = *env;

    p->menu = catnip_menu_new(rt);
    p->settings = catnip_settings_new(rt);
    p->info = catnip_device_info_new(rt);
    if (!p->menu || !p->settings || !p->info) {
        catnip_pages_free(p);
        return NULL;
    }
    return p;
}

void catnip_pages_free(catnip_pages *p)
{
    if (!p) return;
    catnip_device_info_free(p->info);
    catnip_settings_free(p->settings);
    catnip_menu_free(p->menu);
    free(p);
}

catnip_page catnip_pages_current(const catnip_pages *p)
{
    return p ? p->page : CATNIP_PAGE_HOME;
}

catnip_menu *catnip_pages_menu(catnip_pages *p)
{
    return p ? p->menu : NULL;
}

void catnip_pages_rebuild(catnip_pages *p)
{
    int n;
    const catnip_app_entry *apps;

    if (!p) return;
    /* Drawing the launcher *is* being home. Said here rather than at each
     * caller, because the one that forgot it was the way back from the
     * diagnostic: the ring was on screen and this file still believed the
     * device page was up, so the next press went to a page nobody could see. */
    p->page = CATNIP_PAGE_HOME;
    /* A launcher with no shell is a ring with nothing on it but the cat, which
     * is a real state - a device with no card and no built-ins - and the one a
     * host test starts from. */
    n = p->shell ? catnip_shell_count(p->shell) : 0;
    apps = (p->shell && n > 0) ? catnip_shell_app(p->shell, 0) : NULL;
    /* Before the tree is built, because building it names these. Once per menu
     * rebuild rather than per frame: an icon is read off the card, and the card
     * has no business in a render pass. */
    if (p->env.icons_load) p->env.icons_load(p->env.ud, apps, n);
    /* Asked fresh every rebuild rather than remembered: a card can leave
     * between one and the next, and an app that needs it has to grey out when
     * it does. */
    catnip_menu_show(p->menu, apps, n,
                     p->env.card_present ? p->env.card_present(p->env.ud) : false);
    /* The launcher does not introduce itself in its own bar: it *is* the frame,
     * so the header is empty rather than naming the device at a user holding
     * it. An app that takes over says who it is; the launcher has nothing to
     * add. */
    call_title(p, "");
    p->shown_time[0] = '\0';
    catnip_pages_glance(p);
}

void catnip_pages_glance(catnip_pages *p)
{
    static const char *const kDay[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    uint32_t t;
    char hm[8];
    char date[16];
    int32_t y;
    uint32_t mo, d, h, mi, wd;

    if (!p || !p->menu || p->page != CATNIP_PAGE_HOME) return;
    /* Only while the ring is what is on screen: the launcher outlives the
     * menu's tree, so testing it asked "does a launcher exist" - which is
     * always - and answered by going to the I2C bus for a clock nobody could
     * see. */
    if (p->shell && catnip_shell_state(p->shell) == CATNIP_SHELL_RUNNING) return;
    if (!p->env.now_epoch) return;

    t = p->env.now_epoch(p->env.ud);
    if (!t) {
        /* The same admission the clock's own face makes. */
        catnip_menu_set_glance(p->menu, NULL, "----------", "");
        p->shown_time[0] = '\0';
        return;
    }
    catnip_rtc_split(t, &y, &mo, &d, &h, &mi, &wd);
    snprintf(hm, sizeof(hm), "%02u:%02u", (unsigned)h, (unsigned)mi);
    /* The time alone is enough to tell a changed pass from an unchanged one:
     * the date cannot change without it - midnight is 23:59 becoming 00:00 -
     * which is the same reason the weekday is not compared either. */
    if (strcmp(hm, p->shown_time) == 0) return;
    snprintf(p->shown_time, sizeof(p->shown_time), "%s", hm);
    snprintf(date, sizeof(date), "%04d-%02u-%02u", (int)y, (unsigned)mo, (unsigned)d);
    catnip_menu_set_glance(p->menu, hm, date, kDay[wd]);
}

/* What this device is, written out. The strings come from the board because
 * every one of these questions is the device's to answer, and this file has no
 * business knowing what a PSRAM is. */
static void enter_info(catnip_pages *p)
{
    static char rows[CATNIP_INFO_MAX_ROWS][CATNIP_INFO_ROW_MAX];
    const char *ptrs[CATNIP_INFO_MAX_ROWS];
    /* The two places you would go having read this, each with the icon it is
     * known by: the gear the preference page is reached by everywhere else, and
     * the warning triangle for a page that takes the screen and only gives it
     * back when it is told to. */
    static const catnip_info_key kPref = {"Preference", "settings"};
    static const catnip_info_key kDiag = {"Diagnostic", "warning"};
    int n = 0;
    int i;

    p->page = CATNIP_PAGE_INFO;
    if (p->env.info_rows) n = p->env.info_rows(p->env.ud, rows, CATNIP_INFO_MAX_ROWS);
    if (n < 0) n = 0;
    if (n > CATNIP_INFO_MAX_ROWS) n = CATNIP_INFO_MAX_ROWS;
    for (i = 0; i < n; i++)
        ptrs[i] = rows[i];

    catnip_device_info_show(p->info, ptrs, n, &kPref, &kDiag);
    call_title(p, "Device");
}

/* The preference page. It replaces the tree that was there with its own, which
 * is all "entering" means here - all three are platform screens on the same
 * renderer, and none of them is an app. */
static void enter_settings(catnip_pages *p)
{
    p->page = CATNIP_PAGE_PREF;
    p->unsaved = false;
    p->was = *p->cfg;
    catnip_settings_show(p->settings, p->cfg);
    call_title(p, "Preference");
}

/* And back out of it, saving on the way if anything was stepped. Once, here,
 * rather than on every step: see prefs.h. */
static void leave_settings(catnip_pages *p, bool keep, bool home)
{
    if (keep) {
        if (p->unsaved && p->env.save) p->env.save(p->env.ud, p->cfg);
    } else {
        /* Put back what was applied on the way in. Nothing is written, so a
         * page left with B costs no flash even if every column was moved. */
        *p->cfg = p->was;
        if (p->env.apply) p->env.apply(p->env.ud, p->cfg);
    }
    p->unsaved = false;
    /* Back to the page it was opened from. Long B is the one that goes all the
     * way to the cat, and says so by passing home. */
    if (home) {
        p->page = CATNIP_PAGE_HOME;
        catnip_pages_rebuild(p);
    } else {
        enter_info(p);
    }
}

const char *catnip_pages_take_launch(catnip_pages *p)
{
    if (!p || p->page != CATNIP_PAGE_HOME) return NULL;
    return catnip_menu_take_pick(p->menu);
}

bool catnip_pages_step(catnip_pages *p, int gesture)
{
    if (!p) return false;

    if (p->page == CATNIP_PAGE_INFO) {
        int act = catnip_device_info_take_action(p->info);
        if (act == CATNIP_INFO_LEFT) {
            enter_settings(p);
            return true;
        }
        if (act == CATNIP_INFO_RIGHT) {
            if (p->env.enter_diag) p->env.enter_diag(p->env.ud);
            return true;
        }
        /* Either way out of here is the cat: this page is one step down from
         * the ring, not a stack of its own. */
        if (gesture == CATNIP_UI_GESTURE_HOME || gesture == CATNIP_UI_GESTURE_BACK)
            catnip_pages_rebuild(p);
        /* True whether or not anything happened: this pass belonged to this
         * page, and the caller must not also offer the gesture to the shell. */
        return true;
    }

    if (p->page == CATNIP_PAGE_PREF) {
        int result;

        if (catnip_settings_take_dirty(p->settings)) {
            /* Applied on the pass it changed, which is the whole argument for
             * stepping a value in place: a brightness nobody can see while
             * choosing it is a brightness chosen twice. Saving waits for the
             * way out. */
            const catnip_config *cfg = catnip_settings_config(p->settings);
            if (cfg) {
                *p->cfg = *cfg;
                if (p->env.apply) p->env.apply(p->env.ud, p->cfg);
                p->unsaved = true;
            }
        }
        /* A keeps the page and B puts it back - the page says which, because it
         * is the one that knows whether B was a departure or something it
         * handled itself. Long B is home and keeps: it is an escape, and an
         * escape that also undid the last five minutes would be a trap. */
        result = catnip_settings_take_result(p->settings);
        if (gesture == CATNIP_UI_GESTURE_HOME) leave_settings(p, true, true);
        else if (result == CATNIP_SETTINGS_SAVE) leave_settings(p, true, false);
        else if (result == CATNIP_SETTINGS_DISCARD) leave_settings(p, false, false);
        /* The claim is read here and nowhere earlier: it is cleared as it is
         * read, and a running app's on_back answer is the shell's to read. Only
         * a back that arrived while this page is up is this page's to ask
         * about. */
        else if (gesture == CATNIP_UI_GESTURE_BACK && !catnip_render_take_claim(p->rt))
            leave_settings(p, false, false);
        return true;
    }

    /* On the ring. Down opens the hub: what this device is, and the two places
     * you would go having read it. Checked before the pick, so a pass that
     * carries both leaves the menu rather than launching out of a screen that
     * is going away. */
    if (gesture == CATNIP_UI_GESTURE_SETTINGS) {
        enter_info(p);
        return true;
    }
    return false;
}
