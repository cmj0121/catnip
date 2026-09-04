/*
 * main.cpp - Catnip firmware entry for the MeowKit (ESP32-S3).
 *
 * The boot order here is not arbitrary. The power rail is latched first,
 * because everything after it assumes the board stays on. The panel comes up
 * next with its backlight dark, so the first thing the user sees is the
 * landing image rather than a flash of uninitialised memory. Only then does
 * the Lua runtime start.
 *
 * This file only compiles under the Arduino/ESP32 toolchain (PlatformIO). The
 * host test build (`make test`) compiles src/*.c and never this file.
 */
#include <Arduino.h>

#include "catnip_api.h"
#include "catnip_runtime.h"
#include "catnip_shell.h"
#include "device/board.h"
#include "device/display.h"
#include "device/i2cbus.h"
#include "device/ioexp.h"
#include "device/led.h"
#include "device/pmu.h"
#include "device/power.h"
#include "generated/splash_rgb565.h"

/* Where apps live once the SD card is mounted (#32). */
#ifndef CATNIP_APPS_ROOT
#define CATNIP_APPS_ROOT "/sd/catnip/apps"
#endif

static catnip_rt *g_rt;
static catnip_shell *g_shell;

/* No-op HAL for now; the BSP fills these with real drivers (#35). */
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
    /* The renderer replaces this with lv_timer_handler() so the display stays
     * live while a script waits (#30). Until then, just yield. */
    delay(1);
}

/* Raise the backlight gradually - an abrupt jump to full reads as a flash. */
static void fade_in(void)
{
    for (int level = 0; level <= 255; level += 5) {
        catnip_display_backlight((uint8_t)level);
        delay(4);
    }
}

void setup()
{
    /* First, and before anything slow: without this the board switches off. */
    catnip_power_hold();

    Serial.begin(115200);
    /* USB CDC only exists once the host attaches, and after a reset that can
     * take several seconds while the port re-enumerates. Waiting 2.5s was not
     * enough and the opening lines - the ones naming what failed - were lost
     * over and over, which sent more than one investigation the wrong way.
     * The cost of waiting is paid once per boot; the cost of missing them was
     * paid repeatedly. */
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 8000) {
        delay(10);
    }
    Serial.println("[catnip] boot");

    /* A sign of life that does not depend on the screen or on anything having
     * attached to the serial port. It comes up after Serial deliberately: when
     * this was the very first call, a fault inside it left no output at all
     * and looked exactly like a board that never booted. */
    /* A sign of life that does not depend on the screen or on anything having
     * attached to the serial port. It comes up after Serial deliberately: when
     * this was the very first call, a fault inside it left no output at all
     * and looked exactly like a board that never booted. */
    catnip_led_begin();

    /* Order matters: the expander sits on LDO4, so the rail has to be up
     * before the expander can answer, and the expander has to answer before
     * the panel's chip-select can be asserted. The scan comes after the rails
     * so that what it lists is the bus as the rest of the boot will see it. */
    catnip_i2c_begin();
    if (!catnip_pmu_begin()) Serial.println("[catnip] WARN: PMIC not found");
    catnip_i2c_scan();
    if (!catnip_ioexp_begin()) Serial.println("[catnip] WARN: I/O expander not found");

    if (catnip_display_begin()) {
        catnip_display_blit(catnip_splash);

        fade_in();
        Serial.println("[catnip] display up, splash shown");
    } else {
        Serial.println("[catnip] display init FAILED");
    }

    g_rt = catnip_rt_new_tracked(); /* Lua heap lives in PSRAM (#8) */
    if (!g_rt) {
        Serial.println("[catnip] FATAL: runtime allocation failed");
        return;
    }
    catnip_rt_set_log(g_rt, serial_log, nullptr);
    catnip_api_open(g_rt, &g_hal); /* device/sensor/gpio/service/fs (#3) */

    g_shell = catnip_shell_new(g_rt, CATNIP_APPS_ROOT, host_now, host_pump, nullptr);
    if (!g_shell) {
        Serial.println("[catnip] FATAL: shell init failed");
        return;
    }
    /* Expect zero apps until the SD card is mounted (#32). */
    Serial.printf("[catnip] shell ready, %d app(s) under %s\n",
                  catnip_shell_count(g_shell), CATNIP_APPS_ROOT);
}

void loop()
{
    catnip_led_breathe();
    if (g_shell) catnip_shell_step(g_shell);
}
