/*
 * hal_meowkit.cpp - STATUS: scaffold, UNTESTED (needs PlatformIO + hardware).
 * Issue #35: fill catnip_hal with real MeowKit drivers so device.*, sensor.*,
 * gpio.* do something. Wire the returned HAL into main.cpp in place of the
 * current no-op.
 */
#ifdef CATNIP_DEVICE_WIP
#include <Arduino.h>

extern "C" {
#include "../catnip_hal.h"
}

// TODO: implement against the BSP:
//   vibrate (motor), led (WS2812), battery (AXP173), brightness, buttons,
//   imu (BMI270 - measured, not the QMI8658A this line used to name),
//   rtc (PCF8563), gpio header, wifi status/ssid, http_get,
//   sd_reset (reformat the card).
extern "C" const catnip_hal *catnip_meowkit_hal()
{
    static catnip_hal hal; // fill each pointer with a real driver
    return &hal;
}
#endif /* CATNIP_DEVICE_WIP */
