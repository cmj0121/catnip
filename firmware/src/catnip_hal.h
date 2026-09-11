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
#include <stdint.h>

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

/* One BLE advertiser a scan heard (#53): what it calls itself if it says,
 * where it is from, and how strong.
 *
 * `name` is empty far more often than an ssid is. Most advertisers do not carry
 * one - a pair of headphones announces a service and a manufacturer blob and
 * nothing a person would recognise - so the address is the identity here and
 * the name is the nicety, which is the other way round from Wi-Fi.
 *
 * The address as text rather than six bytes: it is shown far more often than it
 * is compared, and every comparison this device does is against another string
 * it was given. */
typedef struct {
    char name[33];
    char addr[18]; /* "AA:BB:CC:DD:EE:FF" */
    int rssi;
} catnip_ble_dev;

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
    /* When the network last set the clock, as a local epoch, or 0 for "not
     * since this boot" (#84). It answers where the time on screen came from,
     * which is a different question from what the time is - and the one asked
     * by anyone about to overwrite it by hand. Not persisted: an RTC that
     * survived a power cycle holds a number whose origin it does not record,
     * and a stored answer would be this firmware claiming to know something it
     * cannot check. */
    long (*ntp_last)(void *ud);
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
    /* Throw the last scan away and look again. Optional: a device with no radio
     * has nothing to look with, and an app that asks gets `false`. */
    void (*wifi_rescan)(void *ud);
    /* The BLE advertisers heard, or -1 while a scan is still running - the same
     * contract wifi_scan answers by, because a caller that has learned one
     * should not have to learn the other. */
    int (*ble_scan)(void *ud, catnip_ble_dev *out, int max);
    void (*ble_rescan)(void *ud);
    /* Present the device to a host as a BLE mouse (#59).
     *
     * begin returns non-zero when the device is now offering itself - which is
     * not the same as a host having taken it up, because that is the host's
     * decision and takes as long as it takes. `state` is what an app shows:
     *
     *   0  not a mouse at all
     *   1  advertising, waiting to be picked up
     *   2  a host is connected
     *
     * Those are three different sentences on a screen, which is why this is one
     * hook returning which rather than a pair of booleans a caller has to
     * combine. move() sends one relative step - dx, dy and wheel are deltas,
     * not coordinates, because a peripheral does not know how big the host's
     * screen is or where its cursor currently sits. */
    int (*ble_mouse_begin)(void *ud);
    void (*ble_mouse_end)(void *ud);
    int (*ble_mouse_state)(void *ud);
    void (*ble_mouse_move)(void *ud, int dx, int dy, int buttons, int wheel);
    /* Broadcast the device as a BLE beacon (#60).
     *
     * begin takes a raw advertisement - `payload`/`len` are the AD structures
     * the app assembled, iBeacon or Eddystone, which this layer does not read -
     * and an interval in milliseconds, and returns non-zero when the beacon is
     * on the air. Called again while up, it re-arms with the new payload, which
     * is how an app changes the id or the rate on a running advertisement.
     * `state` is 0 off / 1 advertising - there is no connected state, because a
     * beacon is never connected to. */
    int (*ble_adv_begin)(void *ud, const uint8_t *payload, int len, int interval_ms);
    void (*ble_adv_end)(void *ud);
    int (*ble_adv_state)(void *ud);
    /* Shape the next begin, for the BLE Spam surface (#89). Both default - never
     * called, which is the whole of #60 - to the non-connectable, silent beacon
     * above. set_type asks for a connectable ADV_IND (`connectable` non-zero,
     * which is what makes a phone offer to pair) and/or a scan response
     * (`scan_rsp`/`scan_rsp_len`, NULL/0 for none). set_addr picks the
     * advertiser address the next begin advertises under - six bytes, or
     * `addr` NULL for a fresh random one, which is how a cycling catalogue looks
     * like many devices rather than one. */
    void (*ble_adv_set_type)(void *ud, int connectable, const uint8_t *scan_rsp,
                             int scan_rsp_len);
    void (*ble_adv_set_addr)(void *ud, const uint8_t *addr);
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
