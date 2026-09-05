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

typedef struct {
    void *ud; /* passed back to every callback */

    /* device.* */
    void (*vibrate)(void *ud, int ms);
    void (*led)(void *ud, int r, int g, int b);
    int (*battery)(void *ud);                  /* 0..100, or -1 if unknown */
    void (*brightness)(void *ud, int pct);     /* 0..100 */
    int (*button)(void *ud, const char *name); /* 1 pressed, 0 released */

    /* sensor.* */
    void (*imu)(void *ud, float out[6]); /* ax,ay,az,gx,gy,gz */
    long (*rtc_now)(void *ud);           /* unix epoch seconds */

    /* gpio.* */
    void (*gpio_mode)(void *ud, int pin, const char *mode); /* in|out|adc */
    void (*gpio_write)(void *ud, int pin, int value);
    int (*gpio_read)(void *ud, int pin);
    int (*gpio_adc)(void *ud, int pin);

    /* service.* */
    int (*wifi_status)(void *ud);       /* 1 connected, 0 not */
    const char *(*wifi_ssid)(void *ud); /* SSID or NULL */
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
