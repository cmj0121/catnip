/*
 * Native test for the Scanner (#57): the group view, and the ordering behind
 * each cell.
 *
 * There is almost nothing else in the app - it asks a driver on a timer and
 * writes what comes back into labels - and the things it decides are the things
 * a screenshot cannot check:
 *
 *   - one radio at a time, in turn, and the turn only moves when that radio has
 *     answered. A round robin that stepped on a nil would leave every cell
 *     permanently half-asked.
 *   - a count that is absent until the first answer, because "not looked yet"
 *     and "heard nobody" are different facts and 0 can only say one of them.
 *   - what order the found things are in, and when that order is allowed to
 *     change. Strongest first is what somebody opens this page for; a plain
 *     sort by RSSI gives it and then takes it away again, because RSSI wanders
 *     a few decibels while nothing moves.
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
#define APP_MAIN "apps/scanner/main.lua"
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

/* What the next scan will answer with, per radio. The app asks the HAL through
 * service.*.scan(), so this is the air. -1 is "still listening", which is a
 * real answer and the one the round robin has to wait on. */
static catnip_wifi_ap g_air[8];
static int g_air_n = -1;
static catnip_ble_dev g_adv[8];
static int g_adv_n = -1;
static int g_wifi_rescans, g_ble_rescans;

static void m_wifi_rescan(void *ud)
{
    (void)ud;
    g_wifi_rescans++;
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

static void m_ble_rescan(void *ud)
{
    (void)ud;
    g_ble_rescans++;
}

static int m_ble_scan(void *ud, catnip_ble_dev *out, int max)
{
    (void)ud;
    if (g_adv_n < 0) return -1;
    int n = g_adv_n < max ? g_adv_n : max;
    for (int i = 0; i < n; i++)
        out[i] = g_adv[i];
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

/* One tick of the app's loop, and the tree drained. */
static void tick(void)
{
    g_millis += 1000;
    for (int i = 0; i < 8; i++)
        catnip_sched_step(g_sched);
    catnip_render_drain(g_rt);
}

static void put_ap(int i, const char *ssid, int rssi, int channel)
{
    snprintf(g_air[i].ssid, sizeof(g_air[i].ssid), "%s", ssid);
    g_air[i].rssi = rssi;
    g_air[i].channel = channel;
}

static void put_dev(int i, const char *name, const char *addr, int rssi)
{
    snprintf(g_adv[i].name, sizeof(g_adv[i].name), "%s", name);
    snprintf(g_adv[i].addr, sizeof(g_adv[i].addr), "%s", addr);
    g_adv[i].rssi = rssi;
}

/* A one-line Lua query, answered as a string / integer / boolean. The app's
 * nodes are read exactly the way an app reads them, through the same
 * metatable. */
static const char *ask_str(const char *expr)
{
    static char buf[128];
    lua_State *L = catnip_rt_lua(g_rt);
    char q[192];

    snprintf(q, sizeof(q), "T = tostring(%s)", expr);
    catnip_rt_dostring(g_rt, q, "=q");
    lua_getglobal(L, "T");
    snprintf(buf, sizeof(buf), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return buf;
}

static void run(const char *src)
{
    catnip_rt_dostring(g_rt, src, "=q");
    catnip_render_drain(g_rt);
}

/* The text of row `i` of whichever list is open. */
static const char *row(int i)
{
    static char buf[128];
    char q[96];

    snprintf(q, sizeof(q),
             "(function() local n = ui.get('row%d') "
             "return n and n.text or '' end)()",
             i);
    snprintf(buf, sizeof(buf), "%s", ask_str(q));
    return buf;
}

static int row_is(int i, const char *want)
{
    return strstr(row(i), want) != NULL;
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
    hal.ble_scan = m_ble_scan;
    hal.ble_rescan = m_ble_rescan;

    g_rt = catnip_rt_new_tracked();
    catnip_ui_open(g_rt);
    catnip_api_open(g_rt, &hal);
    catnip_rt_set_log(g_rt, log_sink, NULL);
    catnip_render_set_dispatch(g_rt, catnip_sched_dispatch, NULL);
    g_sched = catnip_sched_new(g_rt, m_now, m_pump, NULL);
    CHECK(catnip_sched_start(g_sched, app, "=scanner") == 0, "the scanner loads");

    printf("the grid is four protocols, and two of them this board cannot hear\n");
    tick();
    CHECK_STR(ask_str("ui.get('cellwifi').icon"), "wifi", "a cell per protocol");
    CHECK_STR(ask_str("ui.get('cellwifi').text"), "nil",
              "a cell is a picture, and the word for it is in the header");
    CHECK_STR(ask_str("ui.get('cellir').disabled"), "true",
              "IR is drawn and greyed rather than left out");
    CHECK_STR(ask_str("ui.get('cellnfc').disabled"), "true", "and NFC with it");
    CHECK_STR(ask_str("ui.get('cellwifi').disabled"), "false",
              "while the two that work are not");

    printf("a count is absent until the radio has answered, because 0 is a claim\n");
    CHECK_STR(ask_str("ui.get('cellwifi').badge"), "nil",
              "nothing heard yet is not nothing there");
    /* And the device says it is working, in the one place it says that: the
     * first round is the round there is nothing to show during, so the ring is
     * asked for by the rescan that starts it. */
    CHECK(g_wifi_rescans >= 1,
          "a radio that has never answered is asked for a fresh look");

    printf("one radio at a time, and the turn waits for an answer\n");
    /* Wi-Fi is asked first and is still listening, so the turn must not move -
     * a round robin that stepped on a nil would leave every radio permanently
     * half-asked and no cell would ever fill in. */
    tick();
    CHECK_STR(ask_str("ui.get('cellwifi').style"), "body",
              "the radio being listened to is the bright cell");
    CHECK_STR(ask_str("ui.get('cellble').style"), "caption",
              "and the one resting is the quiet one");

    put_ap(0, "far", -85, 1);
    put_ap(1, "near", -45, 6);
    put_ap(2, "middling", -65, 11);
    g_air_n = 3;
    tick();
    CHECK_STR(ask_str("ui.get('cellwifi').badge"), "3", "the answer becomes the count");
    CHECK_STR(ask_str("ui.get('cellble').style"), "body",
              "and the turn moves to the other radio");

    /* The turn hands the ring straight over: the radio whose turn it now is has
     * never answered, so it is rescanned in the same breath rather than on the
     * next tick - a ring that blinked out in between would read as finished. */
    CHECK(g_ble_rescans == 1, "and so is the next one, the moment the turn moves");
    put_dev(0, "tracker", "AA:BB:CC:DD:EE:01", -55);
    put_dev(1, "", "AA:BB:CC:DD:EE:02", -75);
    g_adv_n = 2;
    tick();
    CHECK_STR(ask_str("ui.get('cellble').badge"), "2", "which fills in its own count");
    CHECK_STR(ask_str("ui.get('cellwifi').badge"), "3",
              "and the first count is still there - a group view shows both at once");

    /* And then never again: a grid with counts on it is not waiting, whatever
     * the radios are doing behind it. */
    g_wifi_rescans = 0;
    g_ble_rescans = 0;
    tick();
    tick();
    tick();
    CHECK(g_wifi_rescans == 0 && g_ble_rescans == 0,
          "once every radio has answered once, no round asks for the ring again");

    printf("a cell opens its own list\n");
    /* Nothing is selected on arrival, so the first direction press is what
     * starts choosing - as in the launcher's grid. */
    CHECK_STR(ask_str("ui.title()"), "nil",
              "an untouched grid is the app, not one of its protocols");
    run("ui.fire('protos', 'next')");
    CHECK_STR(ask_str("ui.title()"), "WiFi", "and a focused cell names itself");
    run("ui.fire('protos', 'click')");
    tick();
    CHECK(row_is(1, "near"), "the strongest is first");
    CHECK_STR(row(1), "||||  06  near", "and the line is strength, channel, name");
    CHECK_STR(row(3), "|...  01  far", "with the channel zero-padded to two digits");
    CHECK(row_is(2, "middling"), "then the next");

    printf("A looks again, which is the device saying it heard you\n");
    g_wifi_rescans = 0;
    run("ui.fire('rows', 'click')");
    CHECK(g_wifi_rescans == 1, "A throws the last scan away and asks for another");

    printf("a wobble of a decibel or two does not reorder the page\n");
    put_ap(1, "near", -64, 6);
    put_ap(2, "middling", -66, 11);
    tick();
    CHECK(row_is(1, "near"), "the two within the noise keep the order they had");
    CHECK(row_is(2, "middling"), "both of them");

    printf("and a difference worth seeing does\n");
    put_ap(1, "near", -80, 6);
    put_ap(2, "middling", -50, 11);
    tick();
    CHECK(row_is(1, "middling"), "thirty decibels is not a wobble");
    CHECK(row_is(2, "near"), "so the two change places");

    printf(
        "one that appears goes in at its strength, and one that goes takes its line\n");
    put_ap(3, "loudest", -30, 3);
    g_air_n = 4;
    tick();
    CHECK(row_is(1, "loudest"), "the loudest one arrives at the top");
    put_ap(0, "middling", -50, 11);
    put_ap(1, "loudest", -30, 3);
    g_air_n = 2;
    tick();
    CHECK(row_is(1, "loudest") && row_is(2, "middling"),
          "the two that are left, in order");
    CHECK_STR(row(3), "", "and nothing where the others were");

    printf("the BLE page names what it can and shows an address when it cannot\n");
    run("ui.fire('list', 'back') ui.pop()");
    run("ui.fire('protos', 'next')");
    run("ui.fire('protos', 'click')");
    tick();
    CHECK_STR(ask_str("ui.title()"), "BLE",
              "the rune stands for more than this radio hears, so the word narrows it");
    CHECK_STR(row(1), "||||  tracker", "a name when the advertiser gave one");
    CHECK_STR(row(2), "||..  AA:BB:CC:DD:EE:02",
              "and the address when it did not, rather than a word nobody said");

    printf("a protocol this board cannot hear says so, rather than doing nothing\n");
    run("ui.fire('list', 'back') ui.pop()");
    run("ui.fire('protos', 'next')"); /* BLE was two, so this is IR */
    run("ui.fire('protos', 'click')");
    tick();
    CHECK_STR(row(1), "", "no rows, because there is no radio to hear any");
    CHECK(strstr(ask_str("ui.get('note').text"), "no infrared receiver") != NULL,
          "the page behind a dead cell is where the reason lives");
    CHECK_STR(ask_str("ui.get('note').hidden"), "false", "and it is up");

    free(app);
    catnip_sched_free(g_sched);
    catnip_rt_free(g_rt);
    printf(failures ? "FAILED\n" : "PASSED\n");
    return failures ? 1 : 0;
}
