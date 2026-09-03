/*
 * main.cpp - Catnip firmware entry for the MeowKit (ESP32-S3).
 *
 * Boots the Lua runtime and hands control to the catnip shell, which lists and
 * runs Lua apps from the SD card. Mounting the SD card and rendering the menu /
 * status bar on the LVGL display is bsp/device work; here the shell is driven
 * headless so the wiring is in place.
 *
 * This file only compiles under the Arduino/ESP32 toolchain (PlatformIO). The
 * host test build (`make test`) compiles src/*.c and never this file.
 */
#include <Arduino.h>

#include "catnip_api.h"
#include "catnip_runtime.h"
#include "catnip_shell.h"

/* Where apps live once the SD card is mounted (bsp work). */
#ifndef CATNIP_APPS_ROOT
#define CATNIP_APPS_ROOT "/sd/catnip/apps"
#endif

static catnip_rt *g_rt;
static catnip_shell *g_shell;

/* No-op HAL for now; the BSP fills these with real drivers (device work). */
static catnip_hal g_hal;

static void serial_log(void *ud, const char *msg, size_t len)
{
    (void)ud;
    Serial.write(reinterpret_cast<const uint8_t *>(msg), len);
    Serial.write('\n');
}

static unsigned long host_now(void *ud)
{
    (void)ud;
    return millis();
}

static void host_pump(void *ud)
{
    (void)ud;
    /* The UI renderer replaces this with lv_timer_handler() so the display
     * stays live while a script waits. Until LVGL is wired in, yield. */
    delay(1);
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("[catnip] booting Lua runtime");

    g_rt = catnip_rt_new_tracked(); /* Lua heap lives in PSRAM (#8) */
    if (!g_rt) {
        Serial.println("[catnip] FATAL: runtime allocation failed");
        return;
    }
    catnip_rt_set_log(g_rt, serial_log, nullptr);
    catnip_api_open(g_rt, &g_hal); /* device/sensor/gpio/service/fs namespaces (#3) */

    g_shell = catnip_shell_new(g_rt, CATNIP_APPS_ROOT, host_now, host_pump, nullptr);
    if (!g_shell) {
        Serial.println("[catnip] FATAL: shell init failed");
        return;
    }
    Serial.printf("[catnip] shell ready, %d app(s) found under %s\n",
                  catnip_shell_count(g_shell), CATNIP_APPS_ROOT);
}

void loop()
{
    if (g_shell) catnip_shell_step(g_shell);
}
