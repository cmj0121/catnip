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
#include "generated/anim_f01_rgb565.h"
#include "generated/anim_f02_rgb565.h"
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

/* The idle animation (#40): the frames of catnip_idle_320x240.gif in the
 * order the GIF plays them, but slower - the GIF's 170 ms per frame reads as
 * twitchy on the panel, 400 ms reads as breathing. Frame 0 is the splash, so
 * the first frame is already on screen when the animation starts. It keeps
 * running until the shell takes the screen (#33), which is when g_animating
 * gets cleared; until then it is the only sign the device has not frozen. */
static bool g_animating = true;
static const uint16_t *const g_anim_frames[] = {
    catnip_splash, catnip_anim_f01, catnip_anim_f02, catnip_anim_f01,
};
static const size_t g_anim_count = sizeof(g_anim_frames) / sizeof(g_anim_frames[0]);
static const unsigned long ANIM_FRAME_MS = 400;

static size_t g_frame = 0;

static void draw_current_frame(void)
{
    if (g_animating) catnip_display_blit(g_anim_frames[g_frame]);
}

static void animate(void)
{
    static unsigned long last = 0;
    unsigned long now = millis();
    if (!g_animating || now - last < ANIM_FRAME_MS) return;
    last = now;
    g_frame = (g_frame + 1) % g_anim_count;
    catnip_display_blit(g_anim_frames[g_frame]);
}

/* A short press of the power button turns the screen off and on again. The
 * status LED keeps breathing either way, because a dark screen with nothing
 * else lit is indistinguishable from a device that has crashed or switched
 * itself off.
 *
 * The PMIC does the debouncing: it latches one interrupt per press, and
 * reading it clears the latch (see pmu.h). Holding the button is the
 * hardware's own business and is left alone. */
static bool g_screen_on = true;

static void draw_current_frame(void);

static void set_screen(bool on)
{
    unsigned long t0 = millis();
    /* Remember whether the animation was running rather than assuming it was:
     * once the shell owns the screen (#33) it is not, and switching the screen
     * off and on again must not put the mascot back over the shell's work. */
    static bool was_animating = true;
    if (on) {
        g_animating = was_animating;
    } else {
        was_animating = g_animating;
        g_animating = false;
    }
    g_screen_on = on;
    /* Draw before lighting the panel, not after: the frame that was on screen
     * when it went dark is stale by now, and raising the backlight over it
     * shows the old frame first and the new one a moment later. */
    if (on) draw_current_frame();
    catnip_display_backlight(on ? 255 : 0);
    Serial.printf("[catnip] screen %s (%lu ms)\n", on ? "on" : "off", millis() - t0);
}

static void poll_power_button(void)
{
    if (catnip_pmu_power_key_pressed()) set_screen(!g_screen_on);
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
    catnip_led_begin();

    /* Order matters: the expander sits on LDO4, so the rail has to be up
     * before the expander can answer, and the expander has to release the
     * panel's reset and assert its chip-select before the panel will take a
     * command. The scan comes after the rails so that what it lists is the
     * bus as the rest of the boot will see it. */
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
    /* Either side of the frame draw: a full-screen blit takes long enough that
     * a press landing during one would otherwise wait for it to finish. */
    poll_power_button();
    animate();
    poll_power_button();
    catnip_led_breathe();
    if (g_shell) catnip_shell_step(g_shell);
}
