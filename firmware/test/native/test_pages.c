/*
 * The end-to-end test this project did not have (#81).
 *
 * Everything below is driven by *pressing buttons*: a switch goes down, time
 * passes, it comes up, and what is asserted is what the platform's own screens
 * did about it. No board, no LVGL, no screen - the tree, the focus ring and the
 * page graph are all plain C, and this is the first thing that walks them
 * together.
 *
 * It exists because of how the bugs on the branch before it were found. A ring
 * resting on a list that would not answer it, an A that did nothing, a
 * preference page that returned to the wrong screen, a long B that went
 * somewhere different from a short one - each was found by a person holding the
 * device and describing what they saw, at the cost of a build, a flash that
 * fails a third of the time on this unit, and someone's afternoon.
 *
 * Every one of them is in here, and every one of them fails here first.
 *
 * The other half is the promise no device could ever demonstrate: that leaving
 * the preference page with B **writes nothing**. On hardware that is a claim
 * about flash wear nobody can see; here `save` is a callback with a counter
 * behind it.
 */
#include <stdio.h>
#include <string.h>

#include "catnip_config.h"
#include "catnip_pages.h"
#include "catnip_render.h"
#include "catnip_runtime.h"
#include "catnip_sched.h"
#include "catnip_ui.h"
#include "device/ui_input.h"
#include "device/ui_input_core.h"

static int failures;
#define CHECK(cond, name)                                                                \
    do {                                                                                 \
        if (cond) {                                                                      \
            printf("  ok   - %s\n", name);                                               \
        } else {                                                                         \
            printf("  FAIL - %s\n", name);                                               \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

/* ---- a backend that only remembers names -------------------------------- */

/* The renderer hands out handles; assertions are written about ids. This is the
 * whole of the backend a navigation test needs - it draws nothing, and the one
 * question it answers is "what is this handle called". */
#define SLOTS 64
static struct {
    catnip_handle h;
    char id[32];
    int live;
} g_obj[SLOTS];

static int be_create(void *ud, catnip_handle h, catnip_handle parent, int index,
                     const catnip_node_desc *d)
{
    (void)ud;
    (void)parent;
    (void)index;
    for (int i = 0; i < SLOTS; i++) {
        if (g_obj[i].live) continue;
        g_obj[i].live = 1;
        g_obj[i].h = h;
        snprintf(g_obj[i].id, sizeof(g_obj[i].id), "%s", d->id ? d->id : "");
        return 0;
    }
    return -1;
}

static void be_update(void *ud, catnip_handle h, const catnip_node_desc *d)
{
    (void)ud;
    for (int i = 0; i < SLOTS; i++)
        if (g_obj[i].live && g_obj[i].h == h)
            snprintf(g_obj[i].id, sizeof(g_obj[i].id), "%s", d->id ? d->id : "");
}

static void be_destroy(void *ud, catnip_handle h)
{
    (void)ud;
    for (int i = 0; i < SLOTS; i++)
        if (g_obj[i].live && g_obj[i].h == h) g_obj[i].live = 0;
}

static const catnip_render_backend kBackend = {
    NULL,                        /* ud */
    NULL,                        /* begin_pass */
    NULL,                        /* end_pass */
    be_create,  be_update, NULL, /* move: nothing here cares where a child sits */
    be_destroy, NULL,            /* show_screen */
};

static const char *id_of(catnip_handle h)
{
    for (int i = 0; i < SLOTS; i++)
        if (g_obj[i].live && g_obj[i].h == h) return g_obj[i].id;
    return "";
}

/* ---- the board, as a set of counters ------------------------------------ */

static struct {
    char title[40];
    int applies;
    int saves;
    int diags;
    uint32_t epoch;
} g_board;

static int env_info_rows(void *ud, char (*rows)[CATNIP_INFO_ROW_MAX], int max)
{
    (void)ud;
    if (max < 3) return 0;
    snprintf(rows[0], CATNIP_INFO_ROW_MAX, "catnip v0.0.0-test");
    snprintf(rows[1], CATNIP_INFO_ROW_MAX, "heap 213 KB free");
    snprintf(rows[2], CATNIP_INFO_ROW_MAX, "card none");
    return 3;
}

static void env_apply(void *ud, const catnip_config *cfg)
{
    (void)ud;
    (void)cfg;
    g_board.applies++;
}

static void env_save(void *ud, const catnip_config *cfg)
{
    (void)ud;
    (void)cfg;
    g_board.saves++;
}

static void env_title(void *ud, const char *t)
{
    (void)ud;
    snprintf(g_board.title, sizeof(g_board.title), "%s", t ? t : "");
}

static uint32_t env_now_epoch(void *ud)
{
    (void)ud;
    return g_board.epoch;
}

static void env_enter_diag(void *ud)
{
    (void)ud;
    g_board.diags++;
}

static const catnip_pages_env kEnv = {
    env_info_rows, env_apply,     env_save,       env_title, NULL,
    NULL,          env_now_epoch, env_enter_diag, NULL,
};

/* ---- pressing buttons --------------------------------------------------- */

static catnip_rt *g_rt;
static catnip_sched *g_sched;
static catnip_pages *g_pages;
static catnip_ui_input g_in;
static unsigned g_clock; /* the millisecond counter, ours to advance */

/* One pass of the main loop, in its order: read the switches, run whatever that
 * posted, let the pages act on what is left, reconcile the tree. Getting that
 * order wrong is itself a bug this file can catch, which is why it is written
 * out here rather than hidden in three helpers. */
static void frame(const bool down[CATNIP_BTN_COUNT], unsigned advance)
{
    catnip_ui_sample s;
    int gesture;

    memset(&s, 0, sizeof(s));
    for (int b = 0; b < CATNIP_BTN_COUNT; b++)
        s.down[b] = down ? down[b] : false;
    g_clock += advance;
    s.now = g_clock;

    gesture = catnip_ui_input_run(&g_in, g_rt, &s, NULL);
    catnip_render_drain(g_rt);
    catnip_pages_step(g_pages, gesture);
    catnip_render(g_rt, &kBackend);
}

static void idle(unsigned ms)
{
    frame(NULL, ms);
}

/* A press: down for one pass, then released before the long threshold. */
static void press(catnip_button b)
{
    bool down[CATNIP_BTN_COUNT] = {false};
    down[b] = true;
    frame(down, 20);
    idle(60);
}

/* A hold: down long enough that the long press fires while it is still down. */
static void hold(catnip_button b)
{
    bool down[CATNIP_BTN_COUNT] = {false};
    down[b] = true;
    frame(down, 20);
    frame(down, CATNIP_LONG_PRESS_MS + 50);
    idle(60);
}

static int title_is(const char *t)
{
    return strcmp(g_board.title, t) == 0;
}

static int focus_is(const char *id)
{
    return strcmp(id_of(catnip_ui_input_focus(&g_in)), id) == 0;
}

static unsigned hint(void)
{
    return catnip_ui_input_hint(g_rt, catnip_ui_input_focus(&g_in));
}

int main(void)
{
    catnip_config cfg;
    int screen_was;

    g_rt = catnip_rt_new_tracked();
    catnip_ui_open(g_rt);
    /* Handlers run on a coroutine, one per event, and nothing runs them without
     * this - which is the same arrangement the device is in: catnip_shell.c
     * installs exactly this dispatcher. A test that skipped it would be posting
     * into a queue nobody reads. */
    g_sched = catnip_sched_new(g_rt, NULL, NULL, NULL);
    catnip_render_set_dispatch(g_rt, catnip_sched_dispatch, g_sched);
    catnip_config_defaults(&cfg);
    g_board.epoch = 1757404980u; /* a Tuesday, so the ring has a clock to show */
    g_pages = catnip_pages_new(g_rt, NULL, &cfg, &kEnv);

    printf("the home section, driven by the switches\n");
    CHECK(g_pages != NULL, "the platform's screens are built");

    catnip_pages_rebuild(g_pages);
    catnip_render(g_rt, &kBackend);
    idle(10);

    /* ---- on the cat --------------------------------------------------- */
    CHECK(catnip_pages_current(g_pages) == CATNIP_PAGE_HOME, "it opens on the ring");
    CHECK(title_is(""), "and the launcher does not name itself in its own bar");
    CHECK(focus_is("menu_list"), "the ring is what the focus is on");

    /* The hint, and the reason it exists: up on the cat does nothing, because
     * the grid it will open is not built (#71), and a direction that does
     * nothing is indistinguishable from a device that has stopped listening. */
    CHECK((hint() & CATNIP_HINT_LEFT) && (hint() & CATNIP_HINT_RIGHT),
          "left and right step the ring, so both are lit");
    CHECK(hint() & CATNIP_HINT_DOWN, "down reaches the device page, so it is lit");
    CHECK(!(hint() & CATNIP_HINT_UP), "up has no grid to open yet, so it is dimmed");

    /* ---- down is the device page -------------------------------------- */
    press(CATNIP_BTN_DOWN);
    CHECK(catnip_pages_current(g_pages) == CATNIP_PAGE_INFO,
          "down opens the device page");
    CHECK(title_is("Device"), "and the bar says so");

    /* The ring lands on something that answers. It rested on the facts for a
     * while, where A did nothing at all, which is the bug that made the page
     * look broken on arrival. */
    CHECK(focus_is("info_list"), "the ring opens on the facts, which scroll");
    CHECK(hint() & CATNIP_HINT_DOWN, "so down scrolls them");
    CHECK(hint() & CATNIP_HINT_RIGHT, "and right reaches the buttons");

    press(CATNIP_BTN_RIGHT);
    CHECK(focus_is("info_left"), "right moves the ring onto Preference");
    CHECK(!(hint() & CATNIP_HINT_UP) && !(hint() & CATNIP_HINT_DOWN),
          "a button has nothing for up and down to move, so both are dimmed");
    press(CATNIP_BTN_RIGHT);
    CHECK(focus_is("info_right"), "and again onto Diagnostic");
    CHECK(!(hint() & CATNIP_HINT_RIGHT), "which is the last stop, so right is dimmed");

    /* ---- and A on it is the diagnostic --------------------------------- */
    press(CATNIP_BTN_A);
    CHECK(g_board.diags == 1, "A on the right button hands the screen to the diagnostic");

    /* ---- A on the left button is the preference page ------------------- */
    /* The diagnostic took the screen and handed it back, which on the device is
     * catnip_diag_end() and here is nothing at all - either way the launcher is
     * redrawn, and redrawing it is being home. */
    catnip_pages_rebuild(g_pages);
    idle(10);
    CHECK(catnip_pages_current(g_pages) == CATNIP_PAGE_HOME,
          "redrawing the launcher is being on it");
    press(CATNIP_BTN_DOWN);
    press(CATNIP_BTN_RIGHT);
    press(CATNIP_BTN_A);
    CHECK(catnip_pages_current(g_pages) == CATNIP_PAGE_PREF,
          "A on the left button opens the preference page");
    CHECK(title_is("Preference"), "and the bar says that too");

    /* ---- a value is applied as it is stepped, and B puts it back -------- */
    screen_was = cfg.screen_brightness;
    g_board.applies = 0;
    g_board.saves = 0;
    press(CATNIP_BTN_DOWN);
    CHECK(cfg.screen_brightness < screen_was, "down steps the column the ring is on");
    CHECK(g_board.applies > 0, "and it is applied at once, so it can be seen");
    CHECK(g_board.saves == 0, "nothing is written yet");

    press(CATNIP_BTN_B);
    CHECK(cfg.screen_brightness == screen_was, "B puts back what was there");
    CHECK(g_board.saves == 0, "and writes nothing at all");
    CHECK(catnip_pages_current(g_pages) == CATNIP_PAGE_INFO,
          "leaving returns to the page it was opened from, not to the cat");

    /* ---- A keeps it, and writes once ----------------------------------- */
    press(CATNIP_BTN_RIGHT);
    press(CATNIP_BTN_A);
    CHECK(catnip_pages_current(g_pages) == CATNIP_PAGE_PREF, "back on the page");
    press(CATNIP_BTN_DOWN);
    CHECK(g_board.saves == 0, "a step still writes nothing");
    press(CATNIP_BTN_A);
    CHECK(cfg.screen_brightness < screen_was, "A keeps the change");
    CHECK(g_board.saves == 1, "and writes it exactly once");
    CHECK(catnip_pages_current(g_pages) == CATNIP_PAGE_INFO, "and returns to the hub");

    /* ---- long B is home from anywhere ---------------------------------- */
    hold(CATNIP_BTN_B);
    CHECK(catnip_pages_current(g_pages) == CATNIP_PAGE_HOME,
          "long B from the device page is the cat");
    CHECK(focus_is("menu_list"), "and the ring is back on the carousel");

    press(CATNIP_BTN_DOWN);
    press(CATNIP_BTN_RIGHT);
    press(CATNIP_BTN_A);
    g_board.saves = 0;
    press(CATNIP_BTN_DOWN);
    hold(CATNIP_BTN_B);
    CHECK(catnip_pages_current(g_pages) == CATNIP_PAGE_HOME,
          "long B from the preference page is the cat as well");
    CHECK(g_board.saves == 1,
          "and it keeps, because an escape that undid the last five minutes "
          "would be a trap");

    /* ---- a short B on the cat is not a way out of anything -------------- */
    press(CATNIP_BTN_B);
    CHECK(catnip_pages_current(g_pages) == CATNIP_PAGE_HOME,
          "B on the ring stays on the ring");

    catnip_pages_free(g_pages);
    catnip_sched_free(g_sched);
    catnip_rt_free(g_rt);
    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
