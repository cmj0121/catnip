/*
 * Native test for Air Mouse (#59).
 *
 * Almost all of this app is a radio, and a radio is not something a host can
 * check. What a host can check is everything that decides whether the cursor
 * moves and by how much - and in this app that is a signal chain with an
 * integral in it, which is the most failure-prone thing here by a long way.
 *
 * The two that matter most:
 *
 *   A complete gesture must LEAVE the cursor moved. Moving a device and
 *   stopping is an acceleration followed by an equal deceleration, so the
 *   velocity ends where it started - at zero - and it is the *integral of the
 *   velocity* the cursor draws. Get that wrong and the cursor springs back to
 *   where it started at the end of every stroke, which is the characteristic
 *   failure of a translation air mouse.
 *
 *   A still device must not drift. An integral accumulates its own error, so a
 *   small constant offset is a cursor sliding off the screen while the device
 *   lies on a desk. The deadzone and the decay are what bound it, and a hundred
 *   frames of a small offset is exactly the test for that.
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

/* What the board is doing, as far as the app can tell. Gravity sits on the
 * part's Y axis in this posture; negative is the glass to the user's right,
 * which is the grip the app assumes until told otherwise. */
static float g_ax = 0.0f, g_ay = -1.0f, g_az = 0.0f;
static float g_gx, g_gy, g_gz;
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

static void m_imu(void *ud, float v[6])
{
    (void)ud;
    v[0] = g_ax;
    v[1] = g_ay;
    v[2] = g_az;
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

/* Run a Lua fragment (or none) and read one number back out of a global. */
static double num(const char *src, const char *global)
{
    if (src && src[0]) catnip_rt_dostring(g_rt, src, "=q");
    lua_State *L = catnip_rt_lua(g_rt);
    lua_getglobal(L, global);
    double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

static const char *word(const char *src, const char *global)
{
    static char out[32];
    catnip_rt_dostring(g_rt, src, "=q");
    lua_State *L = catnip_rt_lua(g_rt);
    lua_getglobal(L, global);
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

    printf("a complete movement leaves the cursor moved, and stops\n");
    /* Accelerate for ten frames, decelerate for ten. That is one stroke of a
     * hand: it starts at rest and ends at rest, so the velocity has to come
     * back to zero - and the cursor has to have gone somewhere and stayed
     * there. A model that drew the velocity rather than its integral would
     * bring the cursor home again at the end of every stroke. */
    double total = num("local v, t = 0, 0\n"
                       "for i = 1, 10 do\n"
                       "  v = velocity_step(v, 0.3)\n"
                       "  t = t + (cursor_step(v, 0))\n"
                       "end\n"
                       "for i = 1, 10 do\n"
                       "  v = velocity_step(v, -0.3)\n"
                       "  t = t + (cursor_step(v, 0))\n"
                       "end\n"
                       "TOTAL, VEND = t, v\n",
                       "TOTAL");
    double vend = num("", "VEND");
    CHECK(total > 0, "a push-then-stop moves the cursor away from where it began");
    CHECK(vend < 0.02 && vend > -0.02, "and the velocity has come back to rest");

    double back = num("local v, t = 0, 0\n"
                      "for i = 1, 10 do v = velocity_step(v, -0.3); "
                      "t = t + (cursor_step(v, 0)) end\n"
                      "for i = 1, 10 do v = velocity_step(v, 0.3); "
                      "t = t + (cursor_step(v, 0)) end\n"
                      "BACK = t\n",
                      "BACK");
    CHECK(back < 0, "and the opposite stroke moves it the opposite way");

    printf("a device lying still does not drift\n");
    /* The failure that kills a translation air mouse, and it is silent: the
     * cursor simply leaves. An offset inside the deadzone must never reach the
     * integral at all. */
    double creep = num("local v = 0\n"
                       "for i = 1, 100 do v = velocity_step(v, 0.01) end\n"
                       "CREEP = v\n",
                       "CREEP");
    CHECK(creep == 0, "an offset inside the deadzone never reaches the integral");

    double bounded = num("local v = 0\n"
                         "for i = 1, 1000 do v = velocity_step(v, 0.05) end\n"
                         "BOUNDED = v\n",
                         "BOUNDED");
    CHECK(bounded > 0 && bounded < 0.05,
          "and a small steady one settles rather than accumulating for ever");

    printf("the two axes, and the grip that mirrors them\n");
    double a1 = num("A, D = axes_for(0, 1, false)", "A");
    double d1 = num("A, D = axes_for(1, 0, false)", "D");
    CHECK(a1 > 0, "moving the device on +z steers across positively");
    CHECK(d1 > 0, "and moving it on +y steers down positively");

    double a2 = num("A, D = axes_for(0, 1, true)", "A");
    double d2 = num("A, D = axes_for(1, 0, true)", "D");
    CHECK(a2 == -a1 && d2 == -d1,
          "the other grip is the exact mirror - both axes or neither");

    printf("across is worth more pixels than down, for the same velocity\n");
    /* A 16:9 screen is wider than it is tall, so the same hand movement has to
     * be worth more pixels across to cover the same fraction of it. */
    double px_x = num("X, Y = cursor_step(0.05, 0.05)", "X");
    double px_y = num("", "Y");
    CHECK(px_x > 0 && px_y > 0, "both axes move in their own direction");
    CHECK(px_x > px_y, "and across covers more ground than down");

    double clamped = num("X, Y = cursor_step(1000, 0)", "X");
    CHECK(clamped > 0 && clamped <= 64, "an impossible velocity is clamped, not wrapped");

    printf("which grip it is comes off gravity, and does not flicker\n");
    CHECK(strcmp(word("G = tostring(grip_flipped(-1.0, false))", "G"), "false") == 0,
          "a clear -1g is the glass right");
    CHECK(strcmp(word("G = tostring(grip_flipped(1.0, false))", "G"), "true") == 0,
          "and a clear +1g is the glass left");
    CHECK(strcmp(word("G = tostring(grip_flipped(0.1, true))", "G"), "true") == 0,
          "passing through the middle keeps the last answer");
    CHECK(strcmp(word("G = tostring(grip_flipped(nil, true))", "G"), "true") == 0,
          "and no accelerometer at all changes nothing");

    printf("a flick lifts the mouse, and putting it down takes a moment\n");
    CHECK(strcmp(word("L = lift_next('pointing', 0.1, 0)", "L"), "pointing") == 0,
          "an ordinary movement keeps pointing");
    CHECK(strcmp(word("L = lift_next('pointing', 1.2, 0)", "L"), "lifted") == 0,
          "a flick lifts it at once");
    CHECK(strcmp(word("L = lift_next('lifted', 0.4, 0)", "L"), "lifted") == 0,
          "the tail of the flick does not put it down again");
    CHECK(strcmp(word("L = lift_next('lifted', 0.1, 0)", "L"), "lifted") == 0,
          "and neither does slowing down, on its own");
    CHECK(strcmp(word("L = lift_next('lifted', 0.1, 500)", "L"), "pointing") == 0,
          "it goes back to pointing once the hand has actually settled");

    printf("nothing is reported until a host is connected\n");
    g_state = 1;
    g_az = 0.3f;
    g_moves = 0;
    for (int i = 0; i < 8; i++)
        tick();
    CHECK(g_moves == 0, "moving it while only advertising sends no reports");
    {
        const char *t = catnip_ui_title(g_rt);
        CHECK(t && strstr(t, "pairing") != NULL, "and the header says pairing");
    }

    g_state = 2;
    for (int i = 0; i < 8; i++)
        tick();
    CHECK(g_moves > 0, "once connected, the movement is reported");
    {
        const char *t = catnip_ui_title(g_rt);
        CHECK(t && strstr(t, "connected") != NULL, "and the header says connected");
    }

    printf("A is the left button, and it survives a lift\n");
    g_a_down = 1;
    g_az = 0.0f;
    for (int i = 0; i < 10; i++)
        tick();
    CHECK(g_last_buttons == 1, "A arrives as the left button");

    g_az = 2.0f; /* a flick */
    for (int i = 0; i < 4; i++)
        tick();
    CHECK(g_last_dx == 0 && g_last_buttons == 1,
          "a lift stops the movement and still reports the held button");

    g_a_down = 0;
    for (int i = 0; i < 4; i++)
        tick();
    CHECK(g_last_buttons == 0, "and the release lands even though it is still lifted");

    catnip_sched_free(g_sched);
    catnip_rt_free(g_rt);
    free(app);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
