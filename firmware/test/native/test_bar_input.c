/*
 * Native test for what the two buttons mean while an action bar is up.
 *
 * catnip_bar.c says what the bar *is*; this says what the input pass does with
 * one - which is the half that can go wrong without any of the other half being
 * wrong. The whole claim of the bar being the platform's is that A and B mean
 * the same thing over every app, and A and B are read here.
 *
 * Driven through catnip_ui_input_run with a real tree under it, because the
 * thing being checked is a decision made against a tree: who the long press was
 * asked of, what the app answered with, and where the answer goes back to.
 */
#include <stdio.h>
#include <string.h>

#include "catnip_bar.h"
#include "catnip_render.h"
#include "catnip_runtime.h"
#include "catnip_sched.h"
#include "catnip_ui.h"
#include "device/input.h"
#include "device/press_gesture.h"
#include "device/ui_input.h"
#include "device/ui_input_core.h"
#include "lauxlib.h"
#include "lua.h"

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

/* A backend that only remembers the handle it was given for each id, which is
 * all this test needs to say "post to the list". */
typedef struct {
    catnip_handle h;
    char id[24];
    int used;
} obj;
static obj g_objs[32];

static int b_create(void *ud, catnip_handle h, catnip_handle parent, int index,
                    const catnip_node_desc *d)
{
    (void)ud;
    (void)parent;
    (void)index;
    for (int i = 0; i < 32; i++)
        if (!g_objs[i].used) {
            g_objs[i].used = 1;
            g_objs[i].h = h;
            snprintf(g_objs[i].id, sizeof(g_objs[i].id), "%s", d->id);
            return 0;
        }
    return 0;
}
static void b_destroy(void *ud, catnip_handle h)
{
    (void)ud;
    for (int i = 0; i < 32; i++)
        if (g_objs[i].used && g_objs[i].h == h) g_objs[i].used = 0;
}
static const catnip_render_backend BE = {NULL, NULL, NULL,      b_create,
                                         NULL, NULL, b_destroy, NULL};

static catnip_rt *g_rt;
static catnip_ui_input g_in;
static catnip_bar g_bar;
static catnip_ui_env g_env;
static unsigned g_clock;
static int g_tap_cell = -1;

static int env_action_at(void *ud, int x, int y)
{
    (void)ud;
    (void)x;
    (void)y;
    return g_tap_cell;
}

static void frame(const bool *down, unsigned advance, bool touch)
{
    catnip_ui_sample s;

    memset(&s, 0, sizeof(s));
    for (int b = 0; b < CATNIP_BTN_COUNT; b++)
        s.down[b] = down ? down[b] : false;
    s.touch_down = touch;
    g_clock += advance;
    s.now = g_clock;
    (void)catnip_ui_input_run(&g_in, g_rt, &s, &g_env);
    catnip_render_drain(g_rt);
    (void)catnip_render(g_rt, &BE);
}

static void press(catnip_button b)
{
    bool down[CATNIP_BTN_COUNT] = {false};
    down[b] = true;
    frame(down, 20, false);
    frame(NULL, 60, false);
}

static void hold(catnip_button b)
{
    bool down[CATNIP_BTN_COUNT] = {false};
    down[b] = true;
    frame(down, 20, false);
    frame(down, CATNIP_LONG_PRESS_MS + 50, false);
    frame(NULL, 60, false);
}

/* What the app recorded that it was asked to do. */
static const char *ran(void)
{
    static char buf[64];
    lua_State *L = catnip_rt_lua(g_rt);
    catnip_rt_dostring(g_rt, "R = RAN or ''", "=q");
    lua_getglobal(L, "R");
    snprintf(buf, sizeof(buf), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return buf;
}

static void offer(void)
{
    static const catnip_action kCatalogue[] = {
        {"open", "Open", "folder", false},
        {"rename", "Rename", "edit", false},
        {"delete", "Delete", "trash", true},
        {"cancel", "Cancel", "close", false},
    };
    int index = CATNIP_INDEX_NONE;
    catnip_handle owner = catnip_ui_input_options_target(&g_in, &index);

    (void)catnip_bar_offer(&g_bar, g_rt, kCatalogue, 4, owner, index);
}

/* An app whose long press answers with `ids`, and which writes down what it is
 * told to run. */
static void build(const char *ids)
{
    char code[512];

    snprintf(code, sizeof(code),
             "RAN = ''\n"
             "local rows = ui.list{ id = 'rows', selected = 2,\n"
             "  on_prev = function() end, on_next = function() end,\n"
             "  on_options = function(self, i) OPT_INDEX = i return %s end,\n"
             "  on_action = function(self, id, i) RAN = id .. '@' .. tostring(i) end }\n"
             "ui.screen{ rows }\n"
             "rows:set_children({ ui.label{ id = 'r1' }, ui.label{ id = 'r2' },\n"
             "                    ui.label{ id = 'r3' } })\n",
             ids);
    catnip_rt_dostring(g_rt, code, "=app");
    (void)catnip_render(g_rt, &BE);
    catnip_ui_input_reset(&g_in);
    catnip_bar_close(&g_bar);
    frame(NULL, 100, false); /* settle the ring onto the list */
}

int main(void)
{
    g_rt = catnip_rt_new_tracked();
    catnip_ui_open(g_rt);
    catnip_render_set_dispatch(g_rt, catnip_sched_dispatch, NULL);
    memset(&g_env, 0, sizeof(g_env));
    g_env.action_at = env_action_at;
    g_env.bar = &g_bar;

    printf("long A asks, and what comes back is a bar over the row it was asked of\n");
    build("{ 'open', 'cancel' }");
    hold(CATNIP_BTN_A);
    offer();
    CHECK(catnip_bar_up(&g_bar), "two actions put a bar up");
    CHECK(g_bar.index == 1,
          "about the row that was selected, not wherever the ring goes");

    printf("two actions are the two buttons\n");
    press(CATNIP_BTN_A);
    CHECK(strcmp(ran(), "open@2") == 0, "A runs the left one, about that row");
    CHECK(!catnip_bar_up(&g_bar), "and the bar goes with it");

    build("{ 'open', 'cancel' }");
    hold(CATNIP_BTN_A);
    offer();
    press(CATNIP_BTN_B);
    CHECK(strcmp(ran(), "cancel@2") == 0, "B runs the right one");
    CHECK(!catnip_bar_up(&g_bar), "and the bar goes with that too");

    printf("B never carries something that cannot be undone\n");
    build("{ 'open', 'delete' }");
    hold(CATNIP_BTN_A);
    offer();
    CHECK(catnip_bar_modal(&g_bar), "which is why two of them step instead");
    press(CATNIP_BTN_RIGHT);
    press(CATNIP_BTN_A);
    CHECK(strcmp(ran(), "delete@2") == 0, "right reaches it and A runs it");

    build("{ 'open', 'delete' }");
    hold(CATNIP_BTN_A);
    offer();
    press(CATNIP_BTN_B);
    CHECK(strcmp(ran(), "") == 0, "and B runs nothing at all");
    CHECK(!catnip_bar_up(&g_bar), "it only puts the bar away, which B always does");

    printf("three is a modal: it takes left and right, and the content keeps up/down\n");
    build("{ 'open', 'rename', 'delete' }");
    hold(CATNIP_BTN_A);
    offer();
    CHECK(catnip_bar_modal(&g_bar), "three is a modal");
    press(CATNIP_BTN_RIGHT);
    press(CATNIP_BTN_RIGHT);
    CHECK(g_bar.focus == 2, "right steps it, and clamps where it ends");
    press(CATNIP_BTN_UP);
    CHECK(catnip_bar_up(&g_bar) && g_bar.focus == 2,
          "up belongs to the content underneath and leaves the bar alone");
    press(CATNIP_BTN_A);
    CHECK(strcmp(ran(), "delete@2") == 0, "A runs the focused one");

    printf("long B is the platform's, and no bar may swallow it\n");
    build("{ 'open', 'rename', 'delete' }");
    hold(CATNIP_BTN_A);
    offer();
    {
        bool down[CATNIP_BTN_COUNT] = {false};
        int gesture;
        catnip_ui_sample s;

        memset(&s, 0, sizeof(s));
        down[CATNIP_BTN_B] = true;
        for (int b = 0; b < CATNIP_BTN_COUNT; b++)
            s.down[b] = down[b];
        g_clock += 20;
        s.now = g_clock;
        (void)catnip_ui_input_run(&g_in, g_rt, &s, &g_env);
        g_clock += CATNIP_LONG_PRESS_MS + 50;
        s.now = g_clock;
        gesture = catnip_ui_input_run(&g_in, g_rt, &s, &g_env);
        CHECK(gesture == CATNIP_UI_GESTURE_HOME, "it reaches the caller as home");
    }

    printf("a finger runs a cell directly, which is its only route to Cancel\n");
    build("{ 'open', 'cancel' }");
    hold(CATNIP_BTN_A);
    offer();
    g_tap_cell = 1;
    frame(NULL, 20, true);  /* a contact */
    frame(NULL, 20, false); /* and its release, having gone nowhere */
    g_tap_cell = -1;
    CHECK(strcmp(ran(), "cancel@2") == 0, "a tap on the right cell runs it");
    CHECK(!catnip_bar_up(&g_bar), "and puts the bar away");

    printf("an app that answers with nothing gets no bar\n");
    build("nil");
    hold(CATNIP_BTN_A);
    offer();
    CHECK(!catnip_bar_up(&g_bar), "a long press that does its own thing is left alone");

    catnip_rt_free(g_rt);
    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
