/*
 * Native test for the BLE Beacon (#60).
 *
 * A radio is the one thing a host cannot exercise, so what is checked here is
 * everything up to it: that the app assembles a well-formed iBeacon and a
 * well-formed Eddystone-UID advertisement - the fixed prefix bytes, the lengths
 * that must equal what follows them, the id in the field it belongs in - and
 * that device.beacon.start hands those exact bytes and the interval down to the
 * HAL. The bytes are built by the app's own functions, run through the runtime,
 * rather than a copy of the layout written here that could agree with a wrong
 * app and still pass.
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
#define APP_MAIN "apps/beacon/main.lua"
#endif

/* The proximity UUID the app carries, and the namespace that is its first ten
 * bytes. Kept here so the layout assertions have something to compare the id
 * fields against - if the app changes its identity, this is the one line that
 * moves with it. */
static const unsigned char UUID[16] = {0xCA, 0x7E, 0x1B, 0xEA, 0xC0, 0x00, 0x4C, 0xA7,
                                       0x9A, 0x11, 0xB3, 0xAC, 0x0F, 0xFE, 0xED, 0x00};

typedef struct {
    int adv_begins, adv_ends;
    int adv_len, adv_interval;
    unsigned char adv_payload[64];
} mock;

/* The mock beacon's state, held outside the struct like the mock mouse's: it is
 * set by begin/end and read by state, and a test may want to read it without a
 * struct in hand. */
static int m_adv_state_v;

static int m_adv_begin(void *ud, const uint8_t *payload, int len, int interval_ms)
{
    mock *m = ud;
    m->adv_begins++;
    m->adv_len = len;
    m->adv_interval = interval_ms;
    if (len > 0 && len <= (int)sizeof(m->adv_payload))
        memcpy(m->adv_payload, payload, (size_t)len);
    m_adv_state_v = 1;
    return 1;
}
static void m_adv_end(void *ud)
{
    ((mock *)ud)->adv_ends++;
    m_adv_state_v = 0;
}
static int m_adv_state(void *ud)
{
    (void)ud;
    return m_adv_state_v;
}

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

static void tick(void)
{
    g_millis += 60;
    for (int i = 0; i < 8; i++)
        catnip_sched_step(g_sched);
    catnip_render_drain(g_rt);
}

/* Run `expr` and read the bytes of the string it produces. The result is
 * anchored in the global __B, so the pointer stays valid until the next call
 * overwrites it - which is why each payload is asserted before the next is
 * built. */
static const unsigned char *lua_bytes(const char *expr, size_t *len)
{
    char code[128];
    snprintf(code, sizeof(code), "__B = %s", expr);
    if (catnip_rt_dostring(g_rt, code, "=b") != 0) {
        *len = 0;
        return NULL;
    }
    lua_State *L = catnip_rt_lua(g_rt);
    lua_getglobal(L, "__B");
    const char *s = lua_tolstring(L, -1, len);
    lua_pop(L, 1);
    return (const unsigned char *)s;
}

static int bytes_eq(const unsigned char *got, const unsigned char *want, size_t n)
{
    return got && memcmp(got, want, n) == 0;
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

    mock mk;
    memset(&mk, 0, sizeof(mk));
    m_adv_state_v = 0;

    catnip_hal hal;
    memset(&hal, 0, sizeof(hal));
    hal.ud = &mk;
    hal.ble_adv_begin = m_adv_begin;
    hal.ble_adv_end = m_adv_end;
    hal.ble_adv_state = m_adv_state;

    g_rt = catnip_rt_new_tracked();
    catnip_ui_open(g_rt);
    catnip_api_open(g_rt, &hal);
    catnip_rt_set_log(g_rt, log_sink, NULL);
    catnip_render_set_dispatch(g_rt, catnip_sched_dispatch, NULL);
    g_sched = catnip_sched_new(g_rt, m_now, m_pump, NULL);
    CHECK(catnip_sched_start(g_sched, app, "=beacon") == 0, "the beacon app loads");
    tick();

    printf("the app arms one advertisement the moment it opens\n");
    CHECK(mk.adv_begins >= 1, "device.beacon.start reached the HAL on open");
    CHECK(mk.adv_interval == 250, "with the default interval, 250 ms");
    {
        lua_State *L = catnip_rt_lua(g_rt);
        catnip_rt_dostring(g_rt, "__s = device.beacon.state()", "=s");
        lua_getglobal(L, "__s");
        CHECK(lua_tostring(L, -1) && strcmp(lua_tostring(L, -1), "advertising") == 0,
              "and the state a running beacon reports is 'advertising'");
        lua_pop(L, 1);
    }
    /* The default advertisement is an iBeacon - the app opens on that format. */
    CHECK(mk.adv_len == 30 && mk.adv_payload[0] == 0x02 && mk.adv_payload[4] == 0xFF &&
              mk.adv_payload[5] == 0x4C,
          "the opening advertisement is a well-formed iBeacon");

    printf("iBeacon: Apple's manufacturer AD, the id in the minor\n");
    {
        size_t n = 0;
        const unsigned char *p = lua_bytes("beacon.ibeacon(7)", &n);
        static const unsigned char PREFIX[9] = {0x02, 0x01, 0x06, 0x1A, 0xFF,
                                                0x4C, 0x00, 0x02, 0x15};
        CHECK(n == 30, "an iBeacon advertisement is 30 bytes");
        CHECK(bytes_eq(p, PREFIX, sizeof(PREFIX)),
              "flags, then Apple 0x004C, beacon type 0x02, length 0x15");
        CHECK(p && bytes_eq(p + 9, UUID, 16), "the 16-byte proximity UUID follows");
        CHECK(p && p[25] == 0x00 && p[26] == 0x01, "the major is where it belongs");
        CHECK(p && p[27] == 0x00 && p[28] == 7, "and the id is in the minor");
        CHECK(p && p[29] == 0xC5, "the measured power closes it");
    }

    printf("Eddystone-UID: service data under 0xFEAA, the id in the instance\n");
    {
        size_t n = 0;
        const unsigned char *p = lua_bytes("beacon.eddystone(9)", &n);
        static const unsigned char PREFIX[13] = {0x02, 0x01, 0x06, 0x03, 0x03, 0xAA, 0xFE,
                                                 0x17, 0x16, 0xAA, 0xFE, 0x00, 0xEE};
        CHECK(n == 31, "an Eddystone-UID advertisement is 31 bytes");
        CHECK(bytes_eq(p, PREFIX, sizeof(PREFIX)),
              "flags, the Eddystone UUID list, then UID service data of length 0x17");
        CHECK(p && bytes_eq(p + 13, UUID, 10), "the 10-byte namespace follows");
        {
            static const unsigned char INSTANCE[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 9};
            CHECK(p && bytes_eq(p + 23, INSTANCE, 6), "and the id is in the instance");
        }
        CHECK(p && p[29] == 0x00 && p[30] == 0x00, "two reserved bytes close it");
    }

    printf("device.beacon.start carries the payload and interval down whole\n");
    {
        catnip_rt_dostring(
            g_rt,
            "device.beacon.start{ payload = beacon.eddystone(3), interval_ms = 500 }",
            "=x");
        size_t n = 0;
        const unsigned char *want = lua_bytes("beacon.eddystone(3)", &n);
        CHECK(mk.adv_interval == 500, "the interval reaches the HAL");
        CHECK(mk.adv_len == (int)n && bytes_eq(mk.adv_payload, want, n),
              "and the bytes reach it unchanged, NULs and all");
    }

    printf("stop takes it down, and the state says so\n");
    {
        lua_State *L = catnip_rt_lua(g_rt);
        int ends_before = mk.adv_ends;
        catnip_rt_dostring(g_rt, "device.beacon.stop()", "=x");
        CHECK(mk.adv_ends == ends_before + 1, "device.beacon.stop reached the HAL");
        catnip_rt_dostring(g_rt, "__s = device.beacon.state()", "=s");
        lua_getglobal(L, "__s");
        CHECK(lua_tostring(L, -1) && strcmp(lua_tostring(L, -1), "off") == 0,
              "a stopped beacon is 'off'");
        lua_pop(L, 1);
    }

    /* A device with no beacon radio at all: the app must be able to tell a
     * beacon that never went out from one nobody is hearing, so start answers
     * false and state stays off rather than either one throwing. */
    printf("a device with no beacon radio says so rather than pretending\n");
    {
        catnip_hal none;
        memset(&none, 0, sizeof(none));
        catnip_rt *rt2 = catnip_rt_new_tracked();
        catnip_api_open(rt2, &none);
        int rc = catnip_rt_dostring(
            rt2,
            "NOADV = (device.beacon.start{ payload = string.char(2,1,6) } == false)\n"
            "  and (device.beacon.state() == 'off')\n"
            "device.beacon.stop()\n",
            "=n");
        lua_State *L2 = catnip_rt_lua(rt2);
        lua_getglobal(L2, "NOADV");
        CHECK(rc == 0 && lua_toboolean(L2, -1),
              "start is false, state is off, and stop is a safe no-op");
        catnip_rt_free(rt2);
    }

    free(app);
    catnip_sched_free(g_sched);
    catnip_rt_free(g_rt);
    printf("%s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
