/*
 * Native test for the WiFi Prober's ordering.
 *
 * There is almost nothing else in the app - it asks the driver on a timer and
 * writes what comes back into labels - and the one thing it decides is the one
 * thing a screenshot cannot check: what order the networks are in, and when
 * that order is allowed to change.
 *
 * Strongest first is what somebody opens this page for. A plain sort by RSSI
 * gives it and then takes it away again: RSSI wanders a few decibels while
 * nothing moves, a scan lands every second and a half, and two networks within
 * a decibel of each other would trade places on every one of them. A list that
 * reorders itself while being read is worse than one in the wrong order.
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
#define APP_MAIN "apps/wifiprober/main.lua"
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

static catnip_rt *g_rt;
static catnip_sched *g_sched;
static unsigned long g_millis;

/* What the next scan will answer with. The app asks the HAL through
 * service.wifi.scan(), so this is the air. */
static catnip_wifi_ap g_air[8];
static int g_air_n = -1; /* -1 is "still scanning", which is a real answer */
static int g_rescans;

static void m_wifi_rescan(void *ud)
{
    (void)ud;
    g_rescans++;
}

static int m_wifi_scan(void *ud, catnip_wifi_ap *out, int max)
{
    (void)ud;
    if (g_air_n < 0) return -1;
    int n = g_air_n < max ? g_air_n : max;
    for (int i = 0; i < n; i++)
        out[i] = g_air[i];
    return n;
}

static unsigned long m_now(void *ud)
{
    (void)ud;
    return g_millis;
}
static void m_pump(void *ud)
{
    (void)ud;
}
static void log_sink(void *ud, const char *msg, size_t len)
{
    (void)ud;
    printf("  lua: %.*s\n", (int)len, msg);
}

/* One scan, answered with `n` networks, and the app stepped until it is asleep
 * again. */
static void air(int n)
{
    g_air_n = n;
    g_millis += 2000;
    for (int i = 0; i < 8; i++)
        catnip_sched_step(g_sched);
    catnip_render_drain(g_rt);
}

static void put(int i, const char *ssid, int rssi, int channel)
{
    snprintf(g_air[i].ssid, sizeof(g_air[i].ssid), "%s", ssid);
    g_air[i].rssi = rssi;
    g_air[i].channel = channel;
}

/* The text of row `i`, read the way a user reads it. */
static const char *row(int i)
{
    static char buf[96];
    lua_State *L = catnip_rt_lua(g_rt);
    char q[96];
    snprintf(q, sizeof(q), "local n = ui.get('ap%d') T = n and n.text or ''", i);
    catnip_rt_dostring(g_rt, q, "=q");
    lua_getglobal(L, "T");
    snprintf(buf, sizeof(buf), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return buf;
}

/* Whether the ssid on row `i` is `want`. The rest of the line is bars and a
 * channel, and neither is what the order is about. */
static int row_is(int i, const char *want)
{
    return strstr(row(i), want) != NULL;
}

static int hidden(const char *id)
{
    lua_State *L = catnip_rt_lua(g_rt);
    char q[96];
    int h;
    snprintf(q, sizeof(q), "local n = ui.get('%s') H = n and n.hidden or false", id);
    catnip_rt_dostring(g_rt, q, "=q");
    lua_getglobal(L, "H");
    h = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return h;
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
    hal.wifi_scan = m_wifi_scan;
    hal.wifi_rescan = m_wifi_rescan;

    g_rt = catnip_rt_new_tracked();
    catnip_ui_open(g_rt);
    catnip_api_open(g_rt, &hal);
    catnip_rt_set_log(g_rt, log_sink, NULL);
    catnip_render_set_dispatch(g_rt, catnip_sched_dispatch, NULL);
    g_sched = catnip_sched_new(g_rt, m_now, m_pump, NULL);
    CHECK(catnip_sched_start(g_sched, app, "=prober") == 0, "the prober loads");

    printf("nothing on the air is an answer, and it is the one the header cannot give\n");
    air(0);
    CHECK(!hidden("status"), "a scan that found nothing says so");
    CHECK_STR(row(1), "", "and puts no row on the page to say it with");
    /* And says what to do about it. Two sentences, because "what was found" and
     * "what to do" are two different things. */
    {
        lua_State *L = catnip_rt_lua(g_rt);
        catnip_rt_dostring(g_rt, "S = ui.get('status').text", "=q");
        lua_getglobal(L, "S");
        CHECK(lua_tostring(L, -1) && strstr(lua_tostring(L, -1), "press A") != NULL,
              "and offers the press that looks again");
        lua_pop(L, 1);
        catnip_rt_dostring(g_rt, "A = ui.get('status').align", "=q");
        lua_getglobal(L, "A");
        CHECK(lua_tostring(L, -1) && strcmp(lua_tostring(L, -1), "center") == 0,
              "centred, because a line ranged left on an empty page reads as a list");
        lua_pop(L, 1);
    }

    printf("A looks again, which is the device saying it heard you\n");
    g_rescans = 0;
    catnip_rt_dostring(g_rt, "ui.fire('aps', 'click')", "=q");
    catnip_render_drain(g_rt);
    CHECK(g_rescans == 1, "A throws the last scan away and asks for another");

    /* And on the empty page, where the list is hidden so the message can have
     * the middle - which means nothing is focusable and the press falls through
     * to the screen. The one page that says "press A" was the one page where A
     * had nowhere to land. */
    CHECK(hidden("aps"), "an empty page hides its list");
    g_rescans = 0;
    catnip_rt_dostring(g_rt, "ui.fire(ui.root(), 'click')", "=q");
    catnip_render_drain(g_rt);
    CHECK(g_rescans == 1, "and A reaches the screen instead, which answers it");

    printf("and the list is a focus stop, so a long page can be read to the end\n");
    put(0, "a", -40, 1);
    put(1, "b", -50, 6);
    put(2, "c", -60, 11);
    air(3);
    catnip_rt_dostring(g_rt, "ui.fire('aps', 'next') S = ui.get('aps').selected", "=q");
    catnip_render_drain(g_rt);
    {
        lua_State *L = catnip_rt_lua(g_rt);
        catnip_rt_dostring(g_rt, "S = ui.get('aps').selected", "=q");
        lua_getglobal(L, "S");
        CHECK((int)lua_tointeger(L, -1) == 2, "down moves the reading position");
        lua_pop(L, 1);
    }
    /* And it clamps: a page that came round from the last line to the first
     * would lose the reader their place. */
    catnip_rt_dostring(g_rt,
                       "for _ = 1, 9 do ui.fire('aps', 'next') end "
                       "S = ui.get('aps').selected",
                       "=q");
    catnip_render_drain(g_rt);
    {
        lua_State *L = catnip_rt_lua(g_rt);
        catnip_rt_dostring(g_rt, "S = ui.get('aps').selected", "=q");
        lua_getglobal(L, "S");
        CHECK((int)lua_tointeger(L, -1) == 3, "and stops at the last line");
        lua_pop(L, 1);
    }
    g_air_n = -1;

    printf("a page of networks says nothing at all, because the header counts them\n");
    put(0, "far", -85, 1);
    put(1, "near", -45, 6);
    put(2, "middling", -65, 11);
    air(3);
    CHECK(hidden("status"), "the line is gone the moment there is something to show");
    CHECK(row_is(1, "near"), "the strongest is first");
    /* Strength, channel, name - the two in front of the name are fixed width,
     * so the names line up and the page reads down. */
    CHECK_STR(row(1), "||||  06  near", "and the line is strength, channel, name");
    CHECK_STR(row(3), "|...  01  far", "with the channel zero-padded to two digits");
    CHECK(row_is(2, "middling"), "then the next");
    CHECK(row_is(3, "far"), "and the faintest last");

    printf("a wobble of a decibel or two does not reorder the page\n");
    /* The same three, with `middling` now a hair stronger than `near`. Under a
     * plain sort they would swap; the whole point of the page is that they do
     * not, because a list that reorders itself while being read is worse than
     * one in the wrong order. */
    put(1, "near", -64, 6);
    put(2, "middling", -66, 11);
    air(3);
    CHECK(row_is(1, "near"), "the two within the noise keep the order they had");
    CHECK(row_is(2, "middling"), "both of them");

    printf("and a difference worth seeing does\n");
    put(1, "near", -80, 6);
    put(2, "middling", -50, 11);
    air(3);
    CHECK(row_is(1, "middling"), "thirty decibels is not a wobble");
    CHECK(row_is(2, "near"), "so the two change places");

    printf("a network that appears goes in at its strength, not on the end\n");
    put(3, "loudest", -30, 3);
    air(4);
    CHECK(row_is(1, "loudest"), "the loudest one arrives at the top");

    printf("and one that goes away takes its line with it\n");
    put(0, "middling", -50, 11);
    put(1, "loudest", -30, 3);
    air(2);
    CHECK(row_is(1, "loudest") && row_is(2, "middling"),
          "the two that are left, in order");
    CHECK_STR(row(3), "", "and nothing where the others were");

    free(app);
    catnip_sched_free(g_sched);
    catnip_rt_free(g_rt);
    printf(failures ? "FAILED\n" : "PASSED\n");
    return failures ? 1 : 0;
}
