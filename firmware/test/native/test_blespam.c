/*
 * Native test for the BLE Spam app (#89).
 *
 * The radio is the one thing a host cannot exercise, so what is checked here is
 * everything up to it: that the app builds each public-catalogue payload family
 * with the byte layout that family actually has - the company id, the frame
 * type, the length bytes, the field the variable data lands in - and that the
 * cycle walks the families, wraps, and halts on a stop. The bytes and the cycle
 * are the app's own functions, driven through the runtime, not a copy written
 * here that could agree with a wrong app and still pass.
 *
 * The three things #89 added to the driver that only a radio can show - a
 * connectable ADV_IND on the air, a rotated MAC, the truthful-off on a failed
 * re-arm - are device-only. What crosses the HAL is checked: that the app asks
 * for the right adv type per family and a fresh address every cycle, and that
 * the scan-response option reaches the driver.
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
#define APP_MAIN "apps/blespam/main.lua"
#endif

typedef struct {
    int adv_begins, adv_ends;
    int adv_len, adv_interval;
    unsigned char adv_payload[64];
    int type_calls;
    int last_connectable;
    int last_scan_len;
    unsigned char last_scan[64];
    int addr_calls, addr_random;
} mock;

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
static void m_adv_set_type(void *ud, int connectable, const uint8_t *scan_rsp,
                           int scan_rsp_len)
{
    mock *m = ud;
    m->type_calls++;
    m->last_connectable = connectable;
    m->last_scan_len = scan_rsp_len;
    if (scan_rsp && scan_rsp_len > 0 && scan_rsp_len <= (int)sizeof(m->last_scan))
        memcpy(m->last_scan, scan_rsp, (size_t)scan_rsp_len);
}
static void m_adv_set_addr(void *ud, const uint8_t *addr)
{
    mock *m = ud;
    m->addr_calls++;
    if (!addr) m->addr_random++;
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

/* Run `expr`, anchor its string result in __B, and hand back its bytes. Valid
 * until the next call overwrites __B, so each payload is asserted before the
 * next is built. */
static const unsigned char *lua_bytes(const char *expr, size_t *len)
{
    char code[256];
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

static int lua_bool(const char *expr)
{
    char code[256];
    snprintf(code, sizeof(code), "__T = (%s)", expr);
    if (catnip_rt_dostring(g_rt, code, "=t") != 0) return 0;
    lua_State *L = catnip_rt_lua(g_rt);
    lua_getglobal(L, "__T");
    int v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
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
    hal.ble_adv_set_type = m_adv_set_type;
    hal.ble_adv_set_addr = m_adv_set_addr;

    g_rt = catnip_rt_new_tracked();
    catnip_ui_open(g_rt);
    catnip_api_open(g_rt, &hal);
    catnip_rt_set_log(g_rt, log_sink, NULL);
    catnip_render_set_dispatch(g_rt, catnip_sched_dispatch, NULL);
    g_sched = catnip_sched_new(g_rt, m_now, m_pump, NULL);
    CHECK(catnip_sched_start(g_sched, app, "=blespam") == 0, "the spam app loads");
    tick();

    printf("Apple Continuity - Nearby Action, the device popup\n");
    {
        size_t n = 0;
        const unsigned char *p =
            lua_bytes("blespam.apple_action(0x20, 0xC0, string.char(1,2,3))", &n);
        static const unsigned char WANT[11] = {0x0A, 0xFF, 0x4C, 0x00, 0x0F, 0x05,
                                               0xC0, 0x20, 0x01, 0x02, 0x03};
        CHECK(n == 11, "a Nearby Action advert is 11 bytes");
        CHECK(bytes_eq(p, WANT, 11),
              "length, Apple 0x004C, type 0x0F, the flags, the action, the auth tag");
    }

    printf("Apple Continuity - Proximity Pair, the AirPods card\n");
    {
        size_t n = 0;
        const unsigned char *p = lua_bytes(
            "blespam.apple_pair(0x0E20, 0x33, 0x01, '\\1\\2\\3'..string.rep('\\9',16))",
            &n);
        static const unsigned char PREFIX[10] = {0x1E, 0xFF, 0x4C, 0x00, 0x07,
                                                 0x19, 0x01, 0x0E, 0x20, 0x55};
        CHECK(n == 31, "a Proximity Pair advert fills the whole 31 bytes");
        CHECK(bytes_eq(p, PREFIX, 10), "length 0x1E, Apple, type 0x07, size 0x19, "
                                       "prefix, the 2-byte model, status 0x55");
        CHECK(p && p[10] == 1 && p[11] == 2 && p[12] == 3,
              "the battery/lid bytes follow");
        CHECK(p && p[13] == 0x33 && p[14] == 0x00, "then the colour and a reserved zero");
        CHECK(p && p[15] == 9 && p[30] == 9, "and the 16-byte encrypted blob closes it");
    }

    printf("Microsoft SwiftPair - the Windows toast\n");
    {
        size_t n = 0;
        const unsigned char *p = lua_bytes("blespam.swiftpair('TV')", &n);
        static const unsigned char WANT[9] = {0x08, 0xFF, 0x06, 0x00, 0x03,
                                              0x00, 0x80, 'T',  'V'};
        CHECK(n == 9, "a SwiftPair advert is 7 bytes plus the name");
        CHECK(
            bytes_eq(p, WANT, 9),
            "length, Microsoft 0x0006, beacon 0x03, sub 0x00, RSSI 0x80, then the name");
    }

    printf("Google Fast Pair - the half-sheet\n");
    {
        size_t n = 0;
        const unsigned char *p = lua_bytes("blespam.fastpair(0x0602F0, 0x1B)", &n);
        static const unsigned char WANT[14] = {0x03, 0x03, 0x2C, 0xFE, 0x06, 0x16, 0x2C,
                                               0xFE, 0x06, 0x02, 0xF0, 0x02, 0x0A, 0x1B};
        CHECK(n == 14, "a Fast Pair advert is 14 bytes");
        CHECK(bytes_eq(p, WANT, 14),
              "the FastPair UUID 0xFE2C twice, the 3-byte model, then a Tx-power AD");
    }

    printf("NameFlood - a name in the scanner list\n");
    {
        size_t n = 0;
        const unsigned char *p = lua_bytes("blespam.nameflood('TV')", &n);
        static const unsigned char WANT[14] = {0x02, 0x01, 0x06, 0x03, 0x09, 'T',  'V',
                                               0x03, 0x02, 0x12, 0x18, 0x02, 0x0A, 0x00};
        CHECK(n == 14, "a NameFlood advert is 12 bytes plus the name");
        CHECK(
            bytes_eq(p, WANT, 14),
            "flags, the complete-name AD, the name, the HID UUID 0x1812, a Tx-power AD");
    }

    printf("the cycle walks the catalogue, wraps, and a stop halts it\n");
    {
        int wrap = lua_bool(
            "(function()\n"
            "  blespam.reset()\n"
            "  local n = #blespam.families\n"
            "  local first = blespam.tick().name\n"
            "  for _ = 2, n do blespam.tick() end\n"  /* now sitting on the last family */
            "  local wrapped = blespam.tick().name\n" /* one more wraps to the first */
            "  return n >= 5 and wrapped == first\n"
            "end)()");
        CHECK(wrap, "five families, and the tick after the last returns to the first");
        int halt = lua_bool("(function()\n"
                            "  blespam.reset(); blespam.tick(); blespam.stop()\n"
                            "  return blespam.tick() == nil\n"
                            "end)()");
        CHECK(halt, "after stop() the cycle yields nothing");
    }

    printf("on open, one advert is on the air under a fresh address\n");
    CHECK(mk.adv_begins >= 1, "device.beacon.start reached the HAL on open");
    CHECK(mk.adv_interval == 100, "at the spam interval, 100 ms");
    CHECK(mk.addr_random >= 1, "a random address was asked for before it went out");
    CHECK(mk.type_calls >= 1 && mk.last_connectable == 1,
          "and the first family, Apple Continuity, asked for a connectable advert");

    printf("each broadcast rotates the address and sets the family's adv type\n");
    {
        int addr_before = mk.addr_random;
        catnip_rt_dostring(g_rt, "blespam.broadcast(blespam.families[5])",
                           "=x"); /* Name Flood */
        CHECK(mk.addr_random == addr_before + 1, "a fresh random address per cycle");
        CHECK(mk.last_connectable == 0, "NameFlood asked for a non-connectable advert");
    }

    printf("the scan-response option reaches the driver (#89 carry-forward)\n");
    {
        int types_before = mk.type_calls;
        catnip_rt_dostring(g_rt,
                           "device.beacon.start{ payload = string.char(2,1,6),"
                           " scan_response = string.char(3,9,0x41,0x42) }",
                           "=x");
        CHECK(mk.type_calls == types_before + 1,
              "start with scan_response calls set_type");
        CHECK(mk.last_scan_len == 4 && mk.last_scan[1] == 0x09 && mk.last_scan[3] == 0x42,
              "and the scan-response bytes reach the HAL whole");
    }

    printf("a plain start (no spam options) never touches set_type - #60 unchanged\n");
    {
        int types_before = mk.type_calls;
        catnip_rt_dostring(
            g_rt,
            "device.beacon.start{ payload = string.char(2,1,6), interval_ms = 250 }",
            "=x");
        CHECK(mk.type_calls == types_before,
              "a beacon-shaped start leaves the adv type at the driver's default");
        CHECK(mk.adv_interval == 250, "and its interval still reaches the HAL");
    }

    free(app);
    catnip_sched_free(g_sched);
    catnip_rt_free(g_rt);
    printf("%s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
