/*
 * Native test for Air Mouse (#59).
 *
 * Almost all of this app is a radio, and a radio is not something a host can
 * check. What a host can check is the arithmetic that turns a rotation into a
 * cursor step - and that arithmetic has now been got wrong three times, each
 * time in a way that was invisible in a diff and obvious within a second of
 * holding the device.
 *
 * The one that matters most is the sub-pixel residual. A deliberate slow
 * movement is a fraction of a pixel per frame; rounding each frame to an
 * integer throws every one of those away, and the cursor does not move at all
 * until the hand moves fast enough that it can no longer be aimed. Fine control
 * is entirely made of movements that small, so "a tenth of a pixel a frame
 * eventually moves the cursor" is the single most important line in this file.
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

static float g_ax = 0.0f, g_ay = 0.0f, g_az = 1.0f;
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

    printf("a fraction of a pixel a frame still moves the cursor\n");
    /* THE line. The vertical axis here turns a fiftieth of the horizontal's
     * rotation, so it is a tenth of a pixel per frame - a perfectly ordinary
     * slow, careful movement. An implementation that rounds each frame emits
     * zero for ever; this one has to have moved the cursor within thirty
     * frames, because fine control is made of nothing else. */
    double slow_y = num("local rx, ry, t = 0, 0, 0\n"
                        "for i = 1, 30 do\n"
                        "  local dx, dy\n"
                        "  dx, dy, rx, ry = pointer_delta(0.002, 0.03, rx, ry)\n"
                        "  t = t + dy\n"
                        "end\n"
                        "SLOWY = t\n",
                        "SLOWY");
    CHECK(slow_y >= 2, "a tenth of a pixel a frame accumulates into real pixels");

    printf("one degree is fifty pixels, and the axes do not cross\n");
    /* The vendor's gain on this board: 1 degree of rotation is 50 pixels of
     * cursor. gyro.z is the horizontal and gyro.y the vertical - ours had these
     * the other way round, which is why left and right felt like a different
     * gesture from up and down. */
    double dx1 = num("DX, DY = pointer_delta(0, 1.0, 0, 0)", "DX");
    double dy1 = num("", "DY");
    CHECK(dx1 == 50, "a degree on the z axis is fifty pixels across");
    CHECK(dy1 == 0, "and nothing at all down");

    double dx2 = num("DX, DY = pointer_delta(1.0, 0, 0, 0)", "DX");
    double dy2 = num("", "DY");
    CHECK(dy2 == 50, "a degree on the y axis is fifty pixels down");
    CHECK(dx2 == 0, "and nothing at all across");

    double dxn = num("DX, DY = pointer_delta(0, -1.0, 0, 0)", "DX");
    CHECK(dxn == -50, "and the opposite rotation is the opposite step");

    printf("a hand trembling below the deadzone cannot bank a jump\n");
    /* Under the threshold nothing is emitted AND the residual decays, so a
     * tremor cannot accumulate quietly and then arrive all at once as soon as
     * something else crosses the line. */
    double tremor = num("local rx, ry, t = 0, 0, 0\n"
                        "for i = 1, 200 do\n"
                        "  local dx, dy\n"
                        "  dx, dy, rx, ry = pointer_delta(0.001, 0.001, rx, ry)\n"
                        "  t = t + math.abs(dx) + math.abs(dy)\n"
                        "end\n"
                        "TREMOR, RES = t, math.abs(rx) + math.abs(ry)\n",
                        "TREMOR");
    double res = num("", "RES");
    CHECK(tremor == 0, "two hundred frames under the deadzone move nothing");
    CHECK(res < 0.001, "and leave no residual banked up to spend later");

    double clamped = num("DX, DY = pointer_delta(0, 1000, 0, 0)", "DX");
    CHECK(clamped == 127, "an impossible rotation is clamped to what a report holds");

    printf("the gyroscope's zero is only learned while genuinely still\n");
    /* Both senses have to agree. A device turning at a constant rate is not
     * accelerating, and a device shaken in a straight line is not turning - so
     * either test alone would learn a zero out of a device that is moving, and
     * bake the movement in as the new definition of still. */
    CHECK(strcmp(word("S = tostring(is_stationary(1.0, 1.0))", "S"), "true") == 0,
          "a plain 1g and no rotation is still");
    CHECK(strcmp(word("S = tostring(is_stationary(1.0, 40.0))", "S"), "false") == 0,
          "1g while turning steadily is not");
    CHECK(strcmp(word("S = tostring(is_stationary(1.6, 1.0))", "S"), "false") == 0,
          "and being shaken in a straight line is not either");
    CHECK(strcmp(word("S = tostring(is_stationary(nil, 1.0))", "S"), "false") == 0,
          "no accelerometer means it cannot be claimed");

    printf("a flick lifts the mouse, and putting it down takes a moment\n");
    CHECK(strcmp(word("L = lift_next('pointing', 60, 0)", "L"), "pointing") == 0,
          "an ordinary aiming speed keeps pointing");
    CHECK(strcmp(word("L = lift_next('pointing', 500, 0)", "L"), "lifted") == 0,
          "a flick lifts it at once");
    CHECK(strcmp(word("L = lift_next('lifted', 200, 0)", "L"), "lifted") == 0,
          "the tail of the flick does not put it down again");
    CHECK(strcmp(word("L = lift_next('lifted', 50, 500)", "L"), "pointing") == 0,
          "it goes back to pointing once the hand has settled");

    printf("nothing is reported until a host is connected\n");
    g_state = 1;
    g_gz = 60.0f;
    g_moves = 0;
    for (int i = 0; i < 8; i++)
        tick();
    CHECK(g_moves == 0, "turning it while only advertising sends no reports");
    {
        const char *t = catnip_ui_title(g_rt);
        CHECK(t && strstr(t, "pairing") != NULL, "and the header says pairing");
    }

    g_state = 2;
    for (int i = 0; i < 8; i++)
        tick();
    CHECK(g_moves > 0, "once connected, the movement is reported");
    CHECK(g_last_dx != 0, "and it carries a step across");
    {
        const char *t = catnip_ui_title(g_rt);
        CHECK(t && strstr(t, "connected") != NULL, "and the header says connected");
    }

    printf("a press holds the pointer still, so the click lands on the target\n");
    /* Pressing a button shakes the device, and the shake arrives as a movement
     * that drags the cursor off whatever was being aimed at. */
    g_a_down = 1;
    tick();
    CHECK(g_last_dx == 0 && g_last_buttons == 1,
          "the press reports the button and no movement with it");
    for (int i = 0; i < 4; i++)
        tick();
    CHECK(g_last_dx == 0, "and stays still for a moment afterwards");

    /* Once the settle has passed, aiming works again. */
    for (int i = 0; i < 12; i++)
        tick();
    CHECK(g_last_dx != 0, "then the pointer comes back");

    g_a_down = 0;
    tick();
    CHECK(g_last_buttons == 0, "and the release lands");

    catnip_sched_free(g_sched);
    catnip_rt_free(g_rt);
    free(app);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
