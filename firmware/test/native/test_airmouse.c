/*
 * Native test for Air Mouse (#59).
 *
 * Almost all of this app is a radio, and a radio is not something a host can
 * check. What a host can check is the part that decides where the cursor goes -
 * and that part is where the app's bugs would actually live, because the
 * mapping from an accelerometer to a screen direction is four sign decisions
 * and every one of them is invisible until somebody tilts a device and watches.
 *
 * So the mapping is a pure function in the app and this drives it directly,
 * against the correspondence measured in device/imu_map.c:
 *
 *   ax > 0  =>  the bottom edge is up
 *   ay > 0  =>  the left edge is up
 *
 * The rest is the loop's contract: reports go out only while a host is
 * connected, A arrives as the left button, and the header says which of the
 * three states the mouse is in - which is the only thing this screen exists to
 * say, since the app's actual output happens on somebody else's screen.
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
static float g_ax, g_ay;
static int g_a_down;
static int g_state; /* 0 off, 1 advertising, 2 connected */
static int g_begin_ok = 1;
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

/* The MeowKit's shape: an accelerometer with the gyroscope deliberately off, so
 * three axes are written and three are left as the NaN they arrived as. */
static void m_imu(void *ud, float v[6])
{
    (void)ud;
    v[0] = g_ax;
    v[1] = g_ay;
    v[2] = 1.0f;
}
static int m_button(void *ud, const char *n)
{
    (void)ud;
    return (n[0] == 'a' || n[0] == 'A') ? g_a_down : 0;
}
static int m_mouse_begin(void *ud)
{
    (void)ud;
    if (!g_begin_ok) return 0;
    g_state = 1; /* advertising: offering itself, nobody has taken it */
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

static void tick(void)
{
    g_millis += 40;
    for (int i = 0; i < 8; i++)
        catnip_sched_step(g_sched);
    catnip_render_drain(g_rt);
}

/* The app's mapping, called directly. */
static void cursor(double ax, double ay, int *dx, int *dy)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "DX, DY = cursor_step(%.6f, %.6f)", ax, ay);
    catnip_rt_dostring(g_rt, buf, "=cs");
    lua_State *L = catnip_rt_lua(g_rt);
    lua_getglobal(L, "DX");
    lua_getglobal(L, "DY");
    *dx = (int)lua_tointeger(L, -2);
    *dy = (int)lua_tointeger(L, -1);
    lua_pop(L, 2);
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

    printf("a level device does not move the cursor\n");
    int dx, dy;
    cursor(0.0, 0.0, &dx, &dy);
    CHECK(dx == 0 && dy == 0, "dead level is no step at all");
    cursor(0.0, 0.05, &dx, &dy);
    CHECK(dx == 0 && dy == 0, "and a tilt inside the deadzone is still no step");

    printf("the cursor rolls toward the lowered edge, not the raised one\n");
    /* ay > 0 is the left edge up, so the right is the low side: right. This is
     * the sign that makes "tilt it right and it goes right" true, and it is the
     * negation of imu_map's arrow, which points at the raised edge. */
    cursor(0.0, 0.5, &dx, &dy);
    CHECK(dx > 0 && dy == 0, "left edge up sends it right");
    cursor(0.0, -0.5, &dx, &dy);
    CHECK(dx < 0 && dy == 0, "right edge up sends it left");
    /* ax > 0 is the bottom edge up, so the top is the low side: up the screen,
     * which is a decreasing y. */
    cursor(0.5, 0.0, &dx, &dy);
    CHECK(dy < 0 && dx == 0, "bottom edge up sends it up the screen");
    cursor(-0.5, 0.0, &dx, &dy);
    CHECK(dy > 0 && dx == 0, "top edge up sends it down the screen");

    printf("the step is bounded, symmetric, and starts small\n");
    int dxa, dya, dxb, dyb;
    cursor(0.0, 0.4, &dxa, &dya);
    cursor(0.0, -0.4, &dxb, &dyb);
    CHECK(dxa == -dxb, "equal and opposite tilts give equal and opposite steps");

    cursor(0.0, 5.0, &dx, &dy);
    CHECK(dx > 0 && dx <= 12, "a violent tilt is clamped rather than wrapped");
    int clamped = dx;
    cursor(0.0, 50.0, &dx, &dy);
    CHECK(dx == clamped, "and clamps to the same edge however far it is pushed");

    /* Measured from the edge of the deadzone rather than from zero: the first
     * step out of it has to be small, or the cursor jumps the moment the device
     * is not perfectly level and nothing small can be aimed at. */
    cursor(0.0, 0.14, &dx, &dy);
    CHECK(dx >= 1 && dx <= 3, "the first step past the deadzone is a small one");

    /* A device with no accelerometer reports no axes at all - not zeros. */
    catnip_rt_dostring(g_rt, "DX, DY = cursor_step(nil, nil)", "=cs");
    {
        lua_State *L = catnip_rt_lua(g_rt);
        lua_getglobal(L, "DX");
        lua_getglobal(L, "DY");
        CHECK(lua_tointeger(L, -2) == 0 && lua_tointeger(L, -1) == 0,
              "an absent accelerometer is no movement, not a drift");
        lua_pop(L, 2);
    }

    printf("nothing is reported until a host is actually connected\n");
    g_state = 1; /* advertising */
    g_ax = 0.0f;
    g_ay = 0.6f;
    g_moves = 0;
    for (int i = 0; i < 6; i++)
        tick();
    CHECK(g_moves == 0, "a tilt while only advertising sends no reports");

    printf("and the header says which of the three it is\n");
    {
        const char *t = catnip_ui_title(g_rt);
        CHECK(t && strstr(t, "pairing") != NULL,
              "advertising reads as pairing in the header, not as connected");
    }

    g_state = 2; /* a host took it */
    for (int i = 0; i < 6; i++)
        tick();
    CHECK(g_moves > 0, "once connected, the tilt is reported");
    CHECK(g_last_dx > 0, "and in the direction the mapping says");
    {
        const char *t = catnip_ui_title(g_rt);
        CHECK(t && strstr(t, "connected") != NULL, "and the header says connected");
    }

    printf("A is the left button, and a still device still reports\n");
    g_a_down = 1;
    g_moves = 0;
    for (int i = 0; i < 6; i++)
        tick();
    CHECK(g_last_buttons == 1, "A arrives as the left button");

    g_a_down = 0;
    g_ax = 0.0f;
    g_ay = 0.0f;
    g_moves = 0;
    for (int i = 0; i < 8; i++)
        tick();
    /* Still reporting with nothing to say is the point: it is how a release
     * gets to the host, and how a held button carries on being held. */
    CHECK(g_moves > 0 && g_last_buttons == 0,
          "a still device with nothing held still reports, so a release lands");

    catnip_sched_free(g_sched);
    catnip_rt_free(g_rt);
    free(app);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
