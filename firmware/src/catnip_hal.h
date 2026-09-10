/*
 * catnip_hal.h - the hardware abstraction the device API sits on (issue #3).
 *
 * The Lua namespaces (device.*, sensor.*, gpio.*, service.*) never touch
 * hardware directly; they call through this table of function pointers. The
 * device fills it with real drivers (BSP work); host tests fill it with mocks.
 * Any pointer may be NULL, in which case the matching Lua call is a safe no-op /
 * returns a neutral value.
 */
#ifndef CATNIP_HAL_H
#define CATNIP_HAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One access point a scan found (#54): what it is called, how strong it is in
 * dBm (closer to zero is stronger), and which channel it is on. */
typedef struct {
    char ssid[33];
    int rssi;
    int channel;
} catnip_wifi_ap;

typedef struct {
    void *ud; /* passed back to every callback */

    /* device.* */
    void (*vibrate)(void *ud, int ms);
    void (*led)(void *ud, int r, int g, int b);
    int (*battery)(void *ud);                  /* 0..100, or -1 if unknown */
    void (*brightness)(void *ud, int pct);     /* 0..100 */
    int (*button)(void *ud, const char *name); /* 1 pressed, 0 released */

    /* sensor.* */
    /* Fill `out` with ax, ay, az in g and gx, gy, gz in degrees per second.
     *
     * An axis the device does not measure is left as the NaN the caller passed
     * in, and reaches Lua as a missing field rather than as a number. Writing
     * zero for an axis nothing read would be indistinguishable from a real
     * reading of zero, and a script cannot recover the difference afterwards:
     * a device lying flat genuinely reports gx = 0, so 0.0 is not a value that
     * can be reserved to mean "absent". The MeowKit needs this - its BMI270
     * runs the accelerometer with the gyroscope deliberately off (see
     * device/imu.h), so three of these six have nothing to report. */
    void (*imu)(void *ud, float out[6]);
    long (*rtc_now)(void *ud); /* unix epoch seconds */
    /* Set the clock. Returns non-zero when it took. NULL, or a zero return,
     * both mean "this device cannot be told the time", which an app has to be
     * able to find out - a setter that silently does nothing is worse than one
     * that is not there. */
    int (*rtc_set)(void *ud, long epoch);

    /* gpio.* */
    void (*gpio_mode)(void *ud, int pin, const char *mode); /* in|out|adc */
    void (*gpio_write)(void *ud, int pin, int value);
    int (*gpio_read)(void *ud, int pin);
    int (*gpio_adc)(void *ud, int pin);

    /* service.* */
    int (*wifi_status)(void *ud);       /* 1 connected, 0 not */
    const char *(*wifi_ssid)(void *ud); /* SSID or NULL */
    /* Scan for nearby access points (#54). Fills up to `max` entries and
     * returns how many, or -1 while a scan is still running - which is how a
     * caller knows to ask again rather than that nothing is there. Non-blocking:
     * it starts a scan and reports the last one's results, so no call stops the
     * loop for the seconds a scan takes. */
    int (*wifi_scan)(void *ud, catnip_wifi_ap *out, int max);
    /* HTTP GET: write body into buf (cap incl. NUL); return length or -1. */
    int (*http_get)(void *ud, const char *url, char *buf, size_t cap);

    /* fs.* base directory. NULL disables fs. */
    const char *fs_base;
    /* Reformat / reinitialize the SD card (destructive). Returns 0 on success.
     * NULL means fs.reset() reports "not available". */
    int (*sd_reset)(void *ud);
} catnip_hal;

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_HAL_H */
