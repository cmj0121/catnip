/*
 * Native test for the Clock app's three screens (#73).
 *
 * The clock is the app the five-screen rules were written against, so it is the
 * one that has to be driven rather than read: its face wants the whole panel,
 * its setter wants the bar back, and short A climbs from one to the other. All
 * three of those are contracts between the app and the platform, and none of
 * them is visible in the app's source - `frame = "standard"` on a pushed screen
 * only means anything if something asks the screen rather than the manifest.
 *
 * Driven through the scheduler rather than by dostring, because this app's main
 * chunk never returns: it builds the face and then repaints it forever, which
 * is how an app with no thread keeps a face that has to be right about the
 * minute. A test that ran it straight would hang on the first sys.sleep.
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catnip_api.h"
#include "catnip_render.h"
#include "catnip_runtime.h"
#include "catnip_sched.h"
#include "catnip_ui.h"
#include "lauxlib.h"
#include "lua.h"

#ifndef APP_MAIN
#define APP_MAIN "apps/clock/main.lua"
#endif

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

#define CHECK_STR(got, want, name)                                                       \
    do {                                                                                 \
        const char *g_ = (got), *w_ = (want);                                            \
        if (g_ && strcmp(g_, w_) == 0) {                                                 \
            printf("  ok   - %s\n", name);                                               \
        } else {                                                                         \
            printf("  FAIL - %s\n         want '%s'\n         got  '%s'\n", name, w_,    \
                   g_ ? g_ : "(nil)");                                                   \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

/* ---- a backend that records only what is live ---------------------------- */

typedef struct {
    catnip_handle h;
    char name[32];
    int used;
} robj;
static robj g_objs[64];

static const char *kind_name(catnip_node_kind k)
{
    switch (k) {
    case CATNIP_NODE_SCREEN: return "screen";
    case CATNIP_NODE_BUTTON: return "button";
    case CATNIP_NODE_LIST: return "list";
    default: return "label";
    }
}
static const char *desc_name(const catnip_node_desc *d)
{
    return d->id[0] ? d->id : kind_name(d->kind);
}

static int b_create(void *ud, catnip_handle h, catnip_handle parent, int index,
                    const catnip_node_desc *d)
{
    (void)ud;
    (void)parent;
    (void)index;
    for (int i = 0; i < 64; i++) {
        if (!g_objs[i].used) {
            g_objs[i].used = 1;
            g_objs[i].h = h;
            snprintf(g_objs[i].name, sizeof(g_objs[i].name), "%s", desc_name(d));
            break;
        }
    }
    return 0;
}
static void b_destroy(void *ud, catnip_handle h)
{
    (void)ud;
    for (int i = 0; i < 64; i++)
        if (g_objs[i].used && g_objs[i].h == h) g_objs[i].used = 0;
}
static catnip_handle obj_handle(const char *name)
{
    for (int i = 0; i < 64; i++)
        if (g_objs[i].used && strcmp(g_objs[i].name, name) == 0) return g_objs[i].h;
    return CATNIP_HANDLE_NONE;
}
static int live(const char *name)
{
    return obj_handle(name) != CATNIP_HANDLE_NONE;
}

static const catnip_render_backend BE = {NULL, NULL, NULL,      b_create,
                                         NULL, NULL, b_destroy, NULL};

/* ---- the fixture --------------------------------------------------------- */

static catnip_rt *g_rt;
static catnip_sched *g_sched;
static unsigned long g_millis;
static long g_epoch;    /* what the RTC answers; 0 is "never set" */
static long g_ntp_last; /* what service.ntp.last() answers; 0 is "never" */
static long g_rtc_set_to = -1;

static unsigned long m_now(void *ud)
{
    (void)ud;
    return g_millis;
}
static void m_pump(void *ud)
{
    (void)ud;
}
static long m_rtc_now(void *ud)
{
    (void)ud;
    return g_epoch;
}
static int m_rtc_set(void *ud, long epoch)
{
    (void)ud;
    g_rtc_set_to = epoch;
    g_epoch = epoch;
    return 1;
}
static long m_ntp_last(void *ud)
{
    (void)ud;
    return g_ntp_last;
}

static void log_sink(void *ud, const char *msg, size_t len)
{
    (void)ud;
    printf("  lua: %.*s\n", (int)len, msg);
}

/* Run the app until it is asleep again, then reconcile. The app repaints on a
 * two-second cadence, so `advance` is how a test says "a moment passed". */
static void advance(unsigned long ms)
{
    g_millis += ms;
    for (int i = 0; i < 8; i++)
        catnip_sched_step(g_sched);
    catnip_render_drain(g_rt);
    catnip_render(g_rt, &BE);
}

static void post_at(const char *object, const char *event, int index)
{
    catnip_handle h = obj_handle(object);

    if (h == CATNIP_HANDLE_NONE) {
        printf("  FAIL - no live object named '%s'\n", object);
        failures++;
        return;
    }
    catnip_render_post(g_rt, h, event, index);
    catnip_render_drain(g_rt);
    catnip_render(g_rt, &BE);
}

static void post(const char *object, const char *event)
{
    catnip_handle h = obj_handle(object);
    if (h == CATNIP_HANDLE_NONE) {
        printf("  FAIL - no live object named '%s'\n", object);
        failures++;
        return;
    }
    catnip_render_post(g_rt, h, event, CATNIP_INDEX_NONE);
    catnip_render_drain(g_rt);
    catnip_render(g_rt, &BE);
}

/* Short A on the face. The face has no focusable node - it is labels around a
 * centrepiece - so the platform posts to the visible screen, and that is the
 * path this reproduces rather than reaching for a handler by name. */
static void press_a_on_screen(void)
{
    catnip_handle screen = catnip_render_visible_screen(g_rt);
    catnip_render_post(g_rt, screen, "click", CATNIP_INDEX_NONE);
    catnip_render_drain(g_rt);
    catnip_render(g_rt, &BE);
}

/* Short B, and what the shell would do with the answer: nothing in this app
 * claims back, so the platform pops a pushed screen and otherwise exits.
 * Returns 1 when the platform would have closed the app. */
static int press_back(void)
{
    catnip_handle screen = catnip_render_visible_screen(g_rt);
    int exited = 0;
    if (screen != CATNIP_HANDLE_NONE)
        catnip_render_post_claimable(g_rt, screen, "back", CATNIP_INDEX_NONE);
    catnip_render_drain(g_rt);
    if (!catnip_render_take_claim(g_rt)) {
        if (catnip_ui_depth(g_rt) > 1) catnip_ui_pop(g_rt);
        else exited = 1;
    }
    catnip_render(g_rt, &BE);
    return exited;
}

/* Which role a named node is in - what decides its ink, which is how the strip
 * says which letter is today. */
static const char *style_of(const char *id)
{
    static char buf[32];
    lua_State *L = catnip_rt_lua(g_rt);
    char q[96];
    snprintf(q, sizeof(q), "local n = ui.get('%s') S = n and n.style or ''", id);
    catnip_rt_dostring(g_rt, q, "=q");
    lua_getglobal(L, "S");
    snprintf(buf, sizeof(buf), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return buf;
}

/* What a named node says, read the way a user reads it. */
static const char *text_of(const char *id)
{
    static char buf[128];
    lua_State *L = catnip_rt_lua(g_rt);
    char q[96];
    snprintf(q, sizeof(q), "local n = ui.get('%s') T = n and n.text or ''", id);
    catnip_rt_dostring(g_rt, q, "=q");
    lua_getglobal(L, "T");
    snprintf(buf, sizeof(buf), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return buf;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    return b;
}

int main(void)
{
    char *app = read_file(APP_MAIN);
    if (!app) {
        printf("FAIL - cannot read %s\n", APP_MAIN);
        return 1;
    }

    catnip_hal hal;
    memset(&hal, 0, sizeof(hal));
    hal.rtc_now = m_rtc_now;
    hal.rtc_set = m_rtc_set;
    hal.ntp_last = m_ntp_last;

    /* 2026-09-10 14:32:00 UTC, a Thursday. Every expected string below is
     * derived from this one number, so a fixture that drifts is a fixture that
     * fails loudly rather than one that quietly asserts itself. */
    g_epoch = 1789050720L;

    g_rt = catnip_rt_new_tracked();
    catnip_ui_open(g_rt);
    catnip_api_open(g_rt, &hal);
    catnip_rt_set_log(g_rt, log_sink, NULL);
    catnip_render_set_dispatch(g_rt, catnip_sched_dispatch, NULL);
    g_sched = catnip_sched_new(g_rt, m_now, m_pump, NULL);

    CHECK(catnip_sched_start(g_sched, app, "=clock") == 0, "the clock app loads");
    advance(0);

    printf("the face is the root, and it is a face\n");
    CHECK(live("face_time"), "the root screen shows a time");
    CHECK(live("face_date") && live("face_week") && live("face_src"),
          "with a date, a weekday strip and a source line around it");
    CHECK(live("day1") && live("day7"), "and the strip is seven letters, not one line");
    CHECK(!live("setter"), "and no setter until one is asked for");
    CHECK_STR(text_of("face_time"), "14:32", "the time reads as the RTC says");
    CHECK_STR(text_of("face_date"), "2026-09-10", "and the date beside it");
    /* Thursday: the fifth of seven, counting from Sunday. Which one is lit is a
     * role and not a colour, so the assertion is on the role - the platform is
     * what turns `body` into the bright ink. */
    CHECK_STR(style_of("day5"), "body", "today's letter is the bright one");
    CHECK_STR(style_of("day4"), "caption", "and yesterday's is not");
    CHECK_STR(text_of("day5"), "T", "Thursday, the fifth counting from Sunday");

    printf("where the time came from lives on the face and only here\n");
    CHECK_STR(text_of("face_src"), "", "nothing said when nothing synced it");
    g_ntp_last = 1789050600L; /* 14:30 */
    advance(2000);
    CHECK_STR(text_of("face_src"), "NTP 14:30",
              "and the line appears once a sync has happened");

    printf("the face keeps up with the minute\n");
    g_epoch += 60;
    advance(2000);
    CHECK_STR(text_of("face_time"), "14:33", "a minute later the face says so");

    printf("short A opens the setter, which asks for the bar back\n");
    /* The face does not say `frame` at all: the manifest already said bare, and
     * an app whose usual shape is one of the five says it once. What the
     * platform resolves that to is the assertion - -1 from the screen, and the
     * manifest's answer standing. */
    CHECK(catnip_ui_screen_frame(g_rt) == -1,
          "the face leaves the frame to the manifest");
    CHECK(catnip_ui_bare(g_rt, true), "which makes the panel the app's");
    press_a_on_screen();
    CHECK(live("setter"), "A on the face pushes the setter");
    CHECK(catnip_ui_depth(g_rt) == 2, "above the face rather than replacing it");
    CHECK(catnip_ui_screen_frame(g_rt) == 0,
          "and the setter asks for the standard frame");
    CHECK(!catnip_ui_bare(g_rt, true),
          "which takes the panel back however the manifest was written");
    CHECK(live("set1") && live("set5"), "five columns, one per field");
    /* The setter opens on the time it is about to overwrite. It used to be
     * built once at load, which set the clock back to whenever the app started
     * for anyone who only meant to change the year. */
    CHECK_STR(text_of("set4"), "h", "the hour column is named");
    catnip_rt_dostring(g_rt, "V = ui.get('set4').value_text", "=q");
    {
        lua_State *L = catnip_rt_lua(g_rt);
        lua_getglobal(L, "V");
        CHECK_STR(lua_tostring(L, -1), "14", "and opens on the hour now showing");
        lua_pop(L, 1);
    }

    printf("the source line is not on the setter\n");
    CHECK(!live("face_src") || catnip_ui_depth(g_rt) == 2,
          "the face's line went under with the face, not onto the setter");

    printf("B climbs back down rather than leaving\n");
    CHECK(press_back() == 0, "B from the setter does not exit the app");
    CHECK(catnip_ui_depth(g_rt) == 1, "it pops back to the face");
    CHECK(catnip_ui_bare(g_rt, true), "and the panel is the app's again");
    CHECK(press_back() == 1, "B from the face exits the app");

    printf("an unset clock says so\n");
    g_epoch = 0;
    g_ntp_last = 0;
    advance(2000);
    CHECK_STR(text_of("face_time"), "--:--", "no confident midnight");
    CHECK_STR(style_of("day5"), "caption",
              "and no letter is lit, because none of them is today");
    CHECK_STR(text_of("face_src"), "press A to set it", "it says what to press");

    printf("setting it is two levels, and B climbs back out of both\n");
    press_a_on_screen();
    CHECK(live("setter"), "A still opens the setter with no clock to read");
    /* The events the input pass posts, in the order it posts them: A takes the
     * column up, up and down are then its value, and B puts it back. */
    post_at("setter", "engage", 0);
    post("setter", "raise");
    post("setter", "cancel");
    CHECK(g_rtc_set_to < 0, "B on a column writes nothing at all");

    post_at("setter", "engage", 0);
    post("setter", "raise");
    post("setter", "click");
    CHECK(g_rtc_set_to > 0, "A on a column writes the time");
    CHECK(catnip_ui_depth(g_rt) == 2, "and stays on the setter");

    post("setter", "save");
    CHECK(catnip_ui_depth(g_rt) == 1,
          "two presses of A write it and come back to the face");

    free(app);
    catnip_sched_free(g_sched);
    catnip_rt_free(g_rt);
    printf(failures ? "FAILED\n" : "PASSED\n");
    return failures ? 1 : 0;
}
