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
#include <SD_MMC.h>

#include "catnip_api.h"
#include "catnip_config.h"
#include "catnip_runtime.h"
#include "catnip_shell.h"
#include "device/board.h"
#include "device/display.h"
#include "device/i2cbus.h"
#include "device/ioexp.h"
#include "device/led.h"
#include "device/pmu.h"
#include "device/sd_mount.h"
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

/* The idle animation (#40): the cat waves. The cat has two arms - the left one
 * holds the folder and never moves - so the wave is the right paw swinging
 * wider each frame, with the tail counter-swinging to carry it. The table
 * ping-pongs (f00, f01, f02, f01) so the rest pose is not shown twice in a row
 * at the loop. Frame 0 is the splash, so the first frame is already on screen
 * when the animation starts. It keeps running until the shell takes the screen
 * (#33), which is when g_animating gets cleared; until then it is the only
 * sign the device has not frozen. */
static bool g_animating = true;
static const uint16_t *const g_anim_frames[] = {
    catnip_splash,   /* f00: paw up, rest, no motion arcs */
    catnip_anim_f01, /* f01: paw tipped out, one short arc above it */
    catnip_anim_f02, /* f02: full sweep, arcs off both the paw and the tail */
    catnip_anim_f01, /* back through f01 so the loop is 0,1,2,1 */
};
static const size_t g_anim_count = sizeof(g_anim_frames) / sizeof(g_anim_frames[0]);
static unsigned long g_frame_ms = 625; /* one 4-frame cycle = one 2.5 s breath */

/* Frames from the card, when the owner supplied any: zero means the built-in
 * mascot above. The two are played by the same loop, so the only difference
 * between a stock device and a customised one is where the pixels came from. */
static int g_sd_frames = 0;

static size_t frame_count(void)
{
    return g_sd_frames ? (size_t)g_sd_frames : g_anim_count;
}

static void show_frame(size_t i)
{
    if (g_sd_frames) {
        catnip_display_show_frame((int)i);
    } else {
        catnip_display_blit(g_anim_frames[i]);
    }
}

static size_t g_frame = 0;

static void draw_current_frame(void)
{
    if (g_animating) show_frame(g_frame);
}

static void animate(void)
{
    static unsigned long last = 0;
    unsigned long now = millis();
    if (!g_animating || now - last < g_frame_ms) return;
    last = now;
    g_frame = (g_frame + 1) % frame_count();
    show_frame(g_frame);
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
    catnip_led_dim(!on);
    /* Draw before lighting the panel, not after: the frame that was on screen
     * when it went dark is stale by now, and raising the backlight over it
     * shows the old frame first and the new one a moment later. */
    if (on) draw_current_frame();
    catnip_display_backlight(on ? 255 : 0);
    Serial.printf("[catnip] screen %s (%lu ms)\n", on ? "on" : "off", millis() - t0);
}

/* Switch off in the order the user can see: the screen goes first, so the
 * device reads as shutting down rather than as having hung, and the LED goes
 * dark just before the rail does. */
static void power_off(void)
{
    Serial.println("[catnip] powering off");
    Serial.flush();
    g_animating = false;
    catnip_display_backlight(0);
    catnip_led_level(0);
    catnip_power_off();
}

static void poll_power_button(void)
{
    if (catnip_pmu_power_key_held()) power_off();
    if (catnip_pmu_power_key_pressed()) set_screen(!g_screen_on);
}

/* Read the owner's settings off the card and act on them. Everything here is
 * optional: no file, an unreadable file or a file full of typos all leave the
 * built-in behaviour in place, and say so in the log rather than on screen. */
#ifndef CATNIP_CONFIG_PATH
#define CATNIP_CONFIG_PATH "/sd/catnip/config.json"
#endif

static void apply_config(void)
{
    catnip_config cfg;
    catnip_config_defaults(&cfg);

    File f = SD_MMC.open(CATNIP_CONFIG_PATH);
    if (!f || f.isDirectory()) {
        Serial.println("[catnip] config: none on the card, using the built-in settings");
    } else {
        size_t len = f.size();
        char *text = (char *)malloc(len + 1);
        if (text && f.readBytes(text, len) == len) {
            text[len] = '\0';
            if (catnip_config_parse(&cfg, text, len)) {
                Serial.println("[catnip] config: read from " CATNIP_CONFIG_PATH);
            } else {
                Serial.println(
                    "[catnip] config: not valid JSON, using the built-in settings");
            }
        } else {
            Serial.println(
                "[catnip] config: could not be read, using the built-in settings");
        }
        free(text);
        f.close();
    }

    catnip_led_configure(cfg.led_brightness, cfg.led_breaths_per_second);
    if (cfg.boot_frames_dir[0]) {
        g_sd_frames = catnip_display_load_frames(cfg.boot_frames_dir);
        if (g_sd_frames) {
            g_frame = 0;
            show_frame(0);
        }
    }
    /* Built-in mascot: one ping-pong cycle (f00, f01, f02, f01) is one LED
     * breath, so the cat and the light stay in time. Frames from the card
     * keep the owner's boot.frame_ms, since those are not this cycle. */
    if (g_sd_frames) {
        g_frame_ms = cfg.boot_frame_ms;
    } else {
        unsigned long breath_ms = 2500;
        if (cfg.led_breaths_per_second > 0.0f) {
            breath_ms = (unsigned long)(1000.0f / cfg.led_breaths_per_second);
        }
        if (breath_ms < 4) breath_ms = 4;
        g_frame_ms = breath_ms / 4;
    }
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

    /* After the splash, deliberately: the card is the slowest thing in the
     * boot and nothing on screen should wait for it. */
    if (catnip_sd_mount()) apply_config();

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
