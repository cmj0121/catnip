/* Native test for issue #3: the device & service API. A mock HAL records calls
 * and returns canned readings; the Lua namespaces are exercised against it. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "catnip_api.h"
#include "catnip_runtime.h"
#include "catnip_ui.h"
#include "lua.h"

typedef struct {
    int vibrate_ms, r, g, b, brightness, gpio_pin, gpio_val;
    char gpio_mode[8];
} mock;

static void m_vibrate(void *ud, int ms)
{
    ((mock *)ud)->vibrate_ms = ms;
}
static void m_led(void *ud, int r, int g, int b)
{
    mock *m = ud;
    m->r = r;
    m->g = g;
    m->b = b;
}
static int m_battery(void *ud)
{
    (void)ud;
    return 77;
}
static void m_brightness(void *ud, int p)
{
    ((mock *)ud)->brightness = p;
}
static int m_button(void *ud, const char *n)
{
    (void)ud;
    return strcmp(n, "A") == 0;
}
static void m_imu(void *ud, float v[6])
{
    (void)ud;
    for (int i = 0; i < 6; i++)
        v[i] = (float)(i + 1);
}
/* An accelerometer with no gyroscope, which is the MeowKit's shape: the BMI270
 * runs its accelerometer with the gyroscope deliberately off. It writes three
 * axes and leaves the other three exactly as it found them. */
static void m_imu_accel_only(void *ud, float v[6])
{
    (void)ud;
    v[0] = 0.0f; /* a real reading of zero, which must survive as a number */
    v[1] = -0.5f;
    v[2] = 1.0f;
}

static long m_rtc(void *ud)
{
    (void)ud;
    return 1700000000L;
}
/* When the network last set the clock. 0 is "not since this boot", which the
 * API must hand to Lua as nil rather than as an epoch of zero. */
static long m_ntp_last_v = 0;
static long m_ntp_last(void *ud)
{
    (void)ud;
    return m_ntp_last_v;
}
static void m_gpio_mode(void *ud, int pin, const char *mode)
{
    mock *m = ud;
    m->gpio_pin = pin;
    snprintf(m->gpio_mode, sizeof(m->gpio_mode), "%s", mode);
}
static void m_gpio_write(void *ud, int pin, int v)
{
    mock *m = ud;
    m->gpio_pin = pin;
    m->gpio_val = v;
}
static int m_gpio_read(void *ud, int pin)
{
    (void)ud;
    (void)pin;
    return 1;
}
static int m_gpio_adc(void *ud, int pin)
{
    (void)ud;
    (void)pin;
    return 512;
}
static int m_wifi_status(void *ud)
{
    (void)ud;
    return 1;
}
static const char *m_wifi_ssid(void *ud)
{
    (void)ud;
    return "catnet";
}
static int m_http_get(void *ud, const char *url, char *buf, size_t cap)
{
    (void)ud;
    if (strstr(url, "ping")) {
        snprintf(buf, cap, "pong");
        return 4;
    }
    return -1;
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

static const char *SCRIPT =
    "local ok, err = pcall(function()\n"
    "  device.vibrate(150)\n"
    "  device.led(1, 2, 3)\n"
    "  device.brightness(50)\n"
    "  assert(device.battery() == 77, 'battery')\n"
    "  assert(device.button('A') == true, 'button A')\n"
    "  assert(device.button('X') == false, 'button X')\n"
    "  local m = sensor.imu()\n"
    "  assert(m.ax == 1 and m.gz == 6, 'imu')\n"
    "  assert(sensor.rtc() == 1700000000, 'rtc')\n"
    "  gpio.mode(4, 'out'); gpio.write(4, 1)\n"
    "  assert(gpio.read(5) == 1, 'gpio read')\n"
    "  assert(gpio.adc(6) == 512, 'gpio adc')\n"
    "  assert(service.wifi.status() == true, 'wifi status')\n"
    "  assert(service.wifi.ssid() == 'catnet', 'wifi ssid')\n"
    "  assert(service.http.get('http://x/ping') == 'pong', 'http ok')\n"
    "  assert(service.http.get('http://x/none') == nil, 'http fail is nil')\n"
    "  service.kv.set('a', 42); assert(service.kv.get('a') == 42, 'kv set/get')\n"
    "  service.kv.delete('a'); assert(service.kv.get('a') == nil, 'kv delete')\n"
    "  assert(fs.write('note.txt', 'hi') == true, 'fs write')\n"
    "  assert(fs.read('note.txt') == 'hi', 'fs read')\n"
    "  assert(fs.exists('note.txt') == true, 'fs exists')\n"
    "  assert(fs.exists('nope.txt') == false, 'fs missing')\n"
    "  assert(pcall(function() fs.read('../escape') end) == false, 'fs path guard')\n"
    "end)\n"
    "RESULT = ok and 'ok' or ('FAIL: ' .. tostring(err))\n";

int main(void)
{
    char base[] = "/tmp/catnip_fs_XXXXXX";
    if (!mkdtemp(base)) {
        printf("FAIL - mkdtemp\n");
        return 1;
    }

    mock mk;
    memset(&mk, 0, sizeof(mk));
    catnip_hal hal;
    memset(&hal, 0, sizeof(hal));
    hal.ud = &mk;
    hal.vibrate = m_vibrate;
    hal.led = m_led;
    hal.battery = m_battery;
    hal.brightness = m_brightness;
    hal.button = m_button;
    hal.imu = m_imu;
    hal.rtc_now = m_rtc;
    hal.ntp_last = m_ntp_last;
    hal.gpio_mode = m_gpio_mode;
    hal.gpio_write = m_gpio_write;
    hal.gpio_read = m_gpio_read;
    hal.gpio_adc = m_gpio_adc;
    hal.wifi_status = m_wifi_status;
    hal.wifi_ssid = m_wifi_ssid;
    hal.http_get = m_http_get;
    hal.fs_base = base;

    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_ui_open(rt);
    CHECK(catnip_api_open(rt, &hal) == 0, "api namespaces install");

    int rc = catnip_rt_dostring(rt, SCRIPT, "=api");
    CHECK(rc == 0, "api script runs");

    lua_State *L = catnip_rt_lua(rt);
    lua_getglobal(L, "RESULT");
    const char *result = lua_tostring(L, -1);
    CHECK(result && strcmp(result, "ok") == 0, result ? result : "(no result)");
    lua_pop(L, 1);

    /* Mock recorded the write-side calls. */
    /* service.ntp.last() - the Clock app asks it to say where the time it is
     * about to overwrite came from (#84). Absence is nil, not 0: a script that
     * forgot to check would otherwise format the epoch and print 1970. */
    m_ntp_last_v = 0;
    CHECK(catnip_rt_dostring(rt, "__t = service.ntp.last()", "=t") == 0 &&
              (lua_getglobal(L, "__t"), lua_isnil(L, -1)),
          "service.ntp.last() is nil when nothing synced this boot");
    lua_pop(L, 1);
    m_ntp_last_v = 1789035519L;
    CHECK(catnip_rt_dostring(rt, "__t = service.ntp.last()", "=t") == 0 &&
              (lua_getglobal(L, "__t"), lua_tointeger(L, -1) == 1789035519L),
          "and the epoch of the last sync when there was one");
    lua_pop(L, 1);

    CHECK(mk.vibrate_ms == 150, "vibrate reached the HAL");
    CHECK(mk.r == 1 && mk.g == 2 && mk.b == 3, "led reached the HAL");
    CHECK(mk.brightness == 50, "brightness reached the HAL");
    CHECK(mk.gpio_pin == 4 && mk.gpio_val == 1, "gpio write reached the HAL");
    CHECK(strcmp(mk.gpio_mode, "out") == 0, "gpio mode reached the HAL");

    catnip_rt_free(rt);

    /* A HAL that measures three of the six axes. The other three must arrive in
     * Lua as nil, because a script that cannot tell an absent axis from a real
     * zero will eventually make a decision on one. az proves the reverse too:
     * a genuine 0.0 is still a number. */
    catnip_hal partial;
    memset(&partial, 0, sizeof(partial));
    partial.imu = m_imu_accel_only;
    catnip_rt *rt2 = catnip_rt_new_tracked();
    catnip_api_open(rt2, &partial);
    int rc2 = catnip_rt_dostring(rt2,
                                 "local m = sensor.imu()\n"
                                 "PARTIAL = (m.ax == 0) and (m.az == 1)\n"
                                 "  and (m.gx == nil) and (m.gy == nil)\n"
                                 "  and (m.gz == nil)\n",
                                 "=imu");
    lua_State *L2 = catnip_rt_lua(rt2);
    lua_getglobal(L2, "PARTIAL");
    CHECK(rc2 == 0 && lua_toboolean(L2, -1),
          "an unmeasured IMU axis is nil, a measured zero is 0");
    lua_pop(L2, 1);
    catnip_rt_free(rt2);

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
