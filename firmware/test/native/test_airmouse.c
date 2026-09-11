/*
 * Native test for Air Mouse (#59).
 *
 * Almost all of this app is a radio, and a radio is not something a host can
 * check. What a host can check is everything that decides whether the cursor
 * moves and by how much - and that is where this app's bugs actually live,
 * because it is a pile of thresholds and sign choices, every one invisible
 * until somebody waves a device and watches.
 *
 * Three things are driven directly:
 *
 *   cursor_step  a rate in degrees per second to a step in pixels
 *   lift_next    the "the mouse has been picked up" state machine
 *   the loop     reports only while connected, only while pointing, and the
 *                gyroscope's bias getting zeroed out from under it
 *
 * The bias one matters most. This app turns a rate straight into a movement, so
 * a gyroscope that reads 4 dps while lying on a table is a cursor that crosses
 * the screen on its own - which is the failure the tilt version of this app died
 * of, in a different costume.
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
#define APP_MAIN "apps/airmouse/main.lua"
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

static catnip_rt *g_rt;
static catnip_sched *g_sched;
static unsigned long g_millis;

/* What the board is doing, as far as the app can tell. */
static float g_gx, g_gy, g_gz;
/* Gravity on the part's Y axis. Negative is the glass to the user's
 * right, which is the grip the app assumes until told otherwise. */
static float g_ay = -1.0f;
static int g_a_down;
static int g_state; /* 0 off, 1 advertising, 2 connected */
static int g_moves, g_last_dx, g_last_dy, g_last_buttons;

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

/* Both halves, the way the real HAL now fills them: the accelerometer says
 * which way is down and the gyroscope says how fast it is turning. */
static void m_imu(void *ud, float v[6])
{
    (void)ud;
    v[0] = 0.0f;
    v[1] = g_ay;
    v[2] = 0.0f;
    v[3] = g_gx;
    v[4] = g_gy;
    v[5] = g_gz;
}
static int m_button(void *ud, const char *n)
{
    (void)ud;
    return (n[0] == 'a' || n[0] == 'A') ? g_a_down : 0;
}
static int m_mouse_begin(void *ud)
{
    (void)ud;
    g_state = 1;
    return 1;
}
static void m_mouse_end(void *ud)
{
    (void)ud;
    g_state = 0;
}
static int m_mouse_state(void *ud)
{
    (void)ud;
    return g_state;
}
static void m_mouse_move(void *ud, int dx, int dy, int b, int wheel)
{
    (void)ud;
    (void)wheel;
    g_moves++;
    g_last_dx = dx;
    g_last_dy = dy;
    g_last_buttons = b;
}
static void m_led(void *ud, int r, int g, int b)
{
    (void)ud;
    (void)r;
    (void)g;
    (void)b;
}

static void tick(void)
{
    g_millis += 20;
    for (int i = 0; i < 8; i++)
        catnip_sched_step(g_sched);
    catnip_render_drain(g_rt);
}

/* The app's mapping, called directly. */
static void cursor(double gy, double gz, int flipped, int *dx, int *dy)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "DX, DY = cursor_step(%.6f, %.6f, %s)", gy, gz,
             flipped ? "true" : "false");
    catnip_rt_dostring(g_rt, buf, "=cs");
    lua_State *L = catnip_rt_lua(g_rt);
    lua_getglobal(L, "DX");
    lua_getglobal(L, "DY");
    *dx = (int)lua_tointeger(L, -2);
    *dy = (int)lua_tointeger(L, -1);
    lua_pop(L, 2);
}

/* The lift state machine, called directly. */
static const char *lift(const char *from, double mag, int calm_ms)
{
    static char out[32];
    char buf[192];
    snprintf(buf, sizeof(buf), "LIFT = lift_next('%s', %.1f, %d)", from, mag, calm_ms);
    catnip_rt_dostring(g_rt, buf, "=lift");
    lua_State *L = catnip_rt_lua(g_rt);
    lua_getglobal(L, "LIFT");
    snprintf(out, sizeof(out), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return out;
}

/* The grip decision, called directly. Both arguments as Lua source so the
 * absent-accelerometer case can be passed as a real nil. */
static const char *grip(const char *ay, const char *was)
{
    static char out[32];
    char buf[192];
    snprintf(buf, sizeof(buf), "GRIP = tostring(grip_flipped(%s, %s))", ay, was);
    catnip_rt_dostring(g_rt, buf, "=grip");
    lua_State *L = catnip_rt_lua(g_rt);
    lua_getglobal(L, "GRIP");
    snprintf(out, sizeof(out), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    return out;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
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
    hal.imu = m_imu;
    hal.button = m_button;
    hal.led = m_led;
    hal.ble_mouse_begin = m_mouse_begin;
    hal.ble_mouse_end = m_mouse_end;
    hal.ble_mouse_state = m_mouse_state;
    hal.ble_mouse_move = m_mouse_move;

    g_rt = catnip_rt_new_tracked();
    catnip_ui_open(g_rt);
    catnip_api_open(g_rt, &hal);
    catnip_rt_set_log(g_rt, log_sink, NULL);
    catnip_render_set_dispatch(g_rt, catnip_sched_dispatch, NULL);
    g_sched = catnip_sched_new(g_rt, m_now, m_pump, NULL);
    CHECK(catnip_sched_start(g_sched, app, "=airmouse") == 0, "the mouse loads");
    tick();

    printf("a wand that is not being turned does not move the cursor\n");
    int dx, dy;
    cursor(0.0, 0.0, 0, &dx, &dy);
    CHECK(dx == 0 && dy == 0, "a rate of zero is no step at all");
    cursor(2.0, 2.0, 0, &dx, &dy);
    CHECK(dx == 0 && dy == 0, "and a rate inside the deadzone is still no step");

    printf("the cursor follows the tip: four directions, said four ways\n");
    /* The whole specification of this app, as four sentences. Held with the
     * glass to the user's right, a gyroscope is right-handed about each axis,
     * so swinging the tip right reads positive on gy and raising it reads
     * positive on gz - and screen y counts downward, which is why "up" is the
     * negative one. Getting any of these backwards is invisible in a diff and
     * obvious within one second of holding the device. */
    cursor(60.0, 0.0, 0, &dx, &dy);
    CHECK(dx > 0 && dy == 0, "swing it right, the cursor goes right");
    cursor(-60.0, 0.0, 0, &dx, &dy);
    CHECK(dx < 0 && dy == 0, "swing it left, the cursor goes left");
    /* The tip rising reads NEGATIVE on gz on this board. That was derived the
     * other way round and was wrong on the device - the chain runs through the
     * part's mounting, imu_map's frame, the panel rotation and the direction
     * screen y counts, and being right about three of those four still gives a
     * cursor that goes the wrong way. This is the measurement. */
    cursor(0.0, -60.0, 0, &dx, &dy);
    CHECK(dy < 0 && dx == 0, "raise the tip, the cursor goes up");
    cursor(0.0, 60.0, 0, &dx, &dy);
    CHECK(dy > 0 && dx == 0, "lower the tip, the cursor goes down");

    /* Across must cover more ground than down for the same rate. The wrist has
     * roughly a third the travel side to side that it has up and down, and a
     * 16:9 screen is 1.8 times wider than it is tall; a single gain makes the
     * user pay for both, which is exactly how it felt. */
    cursor(60.0, 60.0, 0, &dx, &dy);
    CHECK(dx > 0 && dy > 0, "both axes still move in their own direction");
    CHECK(dx > dy, "and the same rate covers more ground across than down");

    printf("holding it the other way round mirrors both, and nothing else\n");
    /* The two grips are exact mirrors. If only one axis flipped, one of the
     * four sentences above would come out backwards for left-handed holders
     * and nobody would know which. */
    int mx, my, fx, fy;
    cursor(60.0, 40.0, 0, &mx, &my);
    cursor(60.0, 40.0, 1, &fx, &fy);
    CHECK(fx == -mx && fy == -my, "the flipped grip is the exact negation");

    printf("the step is bounded and starts small\n");
    cursor(100000.0, 0.0, 0, &dx, &dy);
    CHECK(dx > 0 && dx <= 64, "an impossible rate is clamped, not wrapped");
    int clamped = dx;
    cursor(500000.0, 0.0, 0, &dx, &dy);
    CHECK(dx == clamped, "and clamps to the same edge however hard it is pushed");

    /* Measured from the edge of the deadzone rather than from zero. */
    cursor(6.0, 0.0, 0, &dx, &dy);
    CHECK(dx >= 0 && dx <= 2, "the first movement past the deadzone is a small one");

    printf("which grip it is comes off gravity, and does not flicker\n");
    /* The sign of gravity on the part's Y axis is the whole of the difference
     * between the two grips - and the reading passes through zero every time
     * the wand is swung through level, so an app that re-decided there would
     * invert the cursor in the middle of a gesture. */
    CHECK(strcmp(grip("-1.0", "false"), "false") == 0, "a clear -1g is the glass right");
    CHECK(strcmp(grip("1.0", "false"), "true") == 0, "and a clear +1g is the glass left");
    CHECK(strcmp(grip("0.1", "true"), "true") == 0,
          "swinging through level keeps the last answer");
    CHECK(strcmp(grip("0.1", "false"), "false") == 0, "whichever answer that was");
    CHECK(strcmp(grip("nil", "true"), "true") == 0,
          "and no accelerometer at all changes nothing");

    catnip_rt_dostring(g_rt, "DX, DY = cursor_step(nil, nil)", "=cs");
    {
        lua_State *L = catnip_rt_lua(g_rt);
        lua_getglobal(L, "DX");
        lua_getglobal(L, "DY");
        CHECK(lua_tointeger(L, -2) == 0 && lua_tointeger(L, -1) == 0,
              "an absent gyroscope is no movement, not a drift");
        lua_pop(L, 2);
    }

    printf("a flick lifts the mouse, and putting it down takes a moment\n");
    CHECK(strcmp(lift("pointing", 60.0, 0), "pointing") == 0,
          "an ordinary aiming speed keeps pointing");
    CHECK(strcmp(lift("pointing", 400.0, 0), "lifted") == 0, "a flick lifts it at once");
    /* Hysteresis: the flick's own tail sits between the two thresholds, and a
     * single-threshold design would drop back to pointing inside one gesture -
     * which is a cursor that stutters out in bursts during every flick. */
    CHECK(strcmp(lift("lifted", 200.0, 0), "lifted") == 0,
          "the tail of the flick does not put it down again");
    CHECK(strcmp(lift("lifted", 40.0, 0), "lifted") == 0,
          "and neither does slowing down, on its own");
    CHECK(strcmp(lift("lifted", 40.0, 500), "pointing") == 0,
          "it goes back to pointing once the wrist has actually settled");

    printf("nothing is reported until a host is connected\n");
    g_state = 1;
    g_gy = 60.0f;
    g_moves = 0;
    for (int i = 0; i < 8; i++)
        tick();
    CHECK(g_moves == 0, "waving while only advertising sends no reports");
    {
        const char *t = catnip_ui_title(g_rt);
        CHECK(t && strstr(t, "pairing") != NULL, "and the header says pairing");
    }

    g_state = 2;
    for (int i = 0; i < 8; i++)
        tick();
    CHECK(g_moves > 0, "once connected, the wave is reported");
    CHECK(g_last_dx != 0, "and it carries a step");
    {
        const char *t = catnip_ui_title(g_rt);
        CHECK(t && strstr(t, "connected") != NULL, "and the header says connected");
    }

    printf("A is the left button, and it survives a lift\n");
    g_a_down = 1;
    for (int i = 0; i < 4; i++)
        tick();
    CHECK(g_last_buttons == 1, "A arrives as the left button");

    /* A flick while the button is held: the movement must stop and the button
     * must not. A release that happened during a lift and never reached the
     * host would leave it stuck down. */
    g_gy = 600.0f;
    for (int i = 0; i < 4; i++)
        tick();
    CHECK(g_last_dx == 0 && g_last_buttons == 1,
          "a lift stops the movement and still reports the held button");

    g_a_down = 0;
    for (int i = 0; i < 4; i++)
        tick();
    CHECK(g_last_buttons == 0, "and the release lands even though it is still lifted");

    printf("a gyroscope that reads something while still gets zeroed out\n");
    /* The failure this guards: a bias of a few degrees a second is above the
     * deadzone, so without correction it is a cursor sliding across the screen
     * while the device lies on a table. Held below STILL_RATE so the app is
     * entitled to call it "still" and learn it. */
    g_gx = 0.0f;
    g_gy = 4.0f;
    g_gz = 0.0f;
    g_a_down = 0;
    /* Let the lift settle back to pointing first, then let the bias converge. */
    for (int i = 0; i < 200; i++)
        tick();
    g_moves = 0;
    for (int i = 0; i < 10; i++)
        tick();
    CHECK(g_moves > 0, "it is still reporting, so this is a real measurement");
    CHECK(g_last_dx == 0,
          "a steady 4 dps offset stops moving the cursor once it is learned");

    catnip_sched_free(g_sched);
    catnip_rt_free(g_rt);
    free(app);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
