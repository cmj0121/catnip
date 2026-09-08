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
#include "catnip_menu.h"
#include "catnip_runtime.h"
#include "catnip_shell.h"
#include "catnip_ui.h"
#include "device/board.h"
#include "device/diag.h"
#include "device/display.h"
#include "device/i2cbus.h"
#include "device/ioexp.h"
#include "device/led.h"
#include "device/hal_meowkit.h"
#include "device/app_icon.h"
#include "device/frame.h"
#include "device/lvgl_backend.h"
#include "device/lvgl_port.h"
#include "device/pmu.h"
#include "device/sd_mount.h"
#include "device/power.h"
#include "device/ui_input.h"
#include "generated/anim_f01_rgb565.h"
#include "generated/anim_f02_rgb565.h"
#include "generated/splash_rgb565.h"

/* Where apps live once the SD card is mounted (#32). */
#ifndef CATNIP_APPS_ROOT
#define CATNIP_APPS_ROOT "/sd/catnip/apps"
#endif

static catnip_rt *g_rt;
static catnip_shell *g_shell;
/* The renderer's backend (#30). Held here rather than fetched at each use, so
 * that the shell's teardown and the main loop's pass are visibly drawing
 * through the same one. */
static const catnip_render_backend *g_be;
/* The launcher menu (#33): itself a ui.* screen drawn through g_be, so the same
 * renderer and input layer that run an app run the menu. It is what finally
 * exercises the whole path end to end. */
static catnip_menu *g_menu;

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
    /* A script that waits must not freeze the screen, so LVGL gets a turn here
     * as well as in loop(): this is what keeps the display live underneath a
     * blocking call (#29). It does nothing until something has brought LVGL up,
     * and the yield stays either way - lv_timer_handler() is work, not sleep,
     * and the watchdog wants the sleep. */
    catnip_lvgl_step();
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

/* Hand the screen to the input diagnostic (#42). The boot animation stops the
 * same way it will when the shell takes over (#33): g_animating goes false and
 * stays false, so nothing repaints the mascot over the page. */
static void enter_diag(void)
{
    if (catnip_diag_active()) return;
    /* The page draws on lv_screen_active() and keeps it for good, so whatever
     * is loaded there has to be empty before it starts. An app's widgets are
     * still on its own screen at this point, and the page would strip that
     * screen's styles and put its boxes underneath them. Tearing the app down
     * through the shell releases them and leaves the backend's blank screen
     * loaded, which is the screen the page would have had if no app had ever
     * run. Nothing hands the screen back afterwards and nothing needs to:
     * loop() stops stepping the shell the moment the page is up. */
    if (g_shell) catnip_shell_exit(g_shell);
    /* And the frame's bar with it. It lives on lv_layer_top(), which is above
     * every screen including the page's, and the loop's diag branch returns
     * before the line that would hide it - so a bar left up here would float
     * over a page whose whole job is to show what the panel is doing, with the
     * frame testing itself in the top 26 pixels. */
    catnip_frame_show(false);
    /* Hand the touch panel back before the page takes it: the input layer's LVGL
     * pointer indev would otherwise keep injecting taps onto the page's own
     * screen (#31/#42). Safe on the boot-marker path, where no indev was made. */
    catnip_ui_input_end();
    g_animating = false;
    if (!catnip_diag_begin()) g_animating = true;
}

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
    /* Once the input diagnostic has the screen it owns every repaint, so
     * switching the panel off and on again brings the page back rather than
     * the mascot it replaced. */
    if (catnip_diag_active()) {
        catnip_diag_redraw();
        return;
    }
    /* The same handover, one step further along: once an app's widgets are on
     * the panel LVGL owns every repaint, and blitting the mascot over them
     * would leave half a screen of each. */
    if (catnip_lvgl_backend_active()) {
        catnip_lvgl_backend_redraw();
        return;
    }
    if (g_animating) show_frame(g_frame);
}

static void animate(void)
{
    static unsigned long last = 0;
    unsigned long now = millis();
    /* g_animating is deliberately not cleared when the renderer takes over: it
     * is what set_screen() restores when the panel comes back on, and clearing
     * it would make the handover depend on whether the screen happened to be
     * off at the moment it occurred. Who owns the panel is asked afresh here
     * every pass instead. */
    if (catnip_lvgl_backend_active()) return;
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

/* Put the battery on the menu's status line. Wifi is deliberately not shown:
 * the MeowKit HAL leaves wifi_status unwired (see hal_meowkit.cpp), so an
 * indicator would read "disconnected" forever rather than the truth, and an
 * honest gap beats a fake reading - a lesson this project has paid for before.
 * Throttled to once every couple of seconds, because it runs Lua to reach the
 * label and the charge barely moves; `force` refreshes it the moment the menu
 * is rebuilt so the line is never briefly blank. */
static unsigned long g_status_last;

/* The battery in the frame's bar. Every two seconds rather than every pass: the
 * gauge does not move faster than that and each write invalidates the area. */
static void update_status(bool force)
{
    unsigned long now = millis();
    if (!force && now - g_status_last < 2000) return;
    g_status_last = now;
    catnip_frame_set_battery(catnip_pmu_battery_percent());
}

/* (Re)draw the menu with the apps the shell found. Called at boot and every
 * time an app returns, because the renderer's teardown between apps has cleared
 * the tree by then. catnip_shell_app(,0) is the base of the shell's contiguous
 * app array, which is what the menu reads. */
static void rebuild_menu(void)
{
    if (!g_menu || !g_shell) return;
    int n = catnip_shell_count(g_shell);
    const catnip_app_entry *apps = (n > 0) ? catnip_shell_app(g_shell, 0) : nullptr;
    /* Before the tree is built, because building it names these. Once per menu
     * rebuild rather than per frame: an icon is read off the card, and the card
     * has no business in a render pass. */
    catnip_app_icons_load(apps, n);
    /* Asked fresh every rebuild rather than remembered: a card can leave
     * between one and the next, and an app that needs it has to grey out when
     * it does. */
    catnip_menu_show(g_menu, apps, n, catnip_sd_mounted());
    /* The launcher does not introduce itself in its own bar: it *is* the frame,
     * so the header is empty rather than naming the device at a user holding
     * it. An app that takes over says who it is; the launcher has nothing to
     * add. */
    catnip_frame_set_title("");
    update_status(true);
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
    if (catnip_sd_mount()) {
        apply_config();
        /* The marker file means the owner wants the input page and nothing
         * else, so the Lua runtime and the shell below are never started: they
         * would only delay the page and then compete with it for the screen.
         * The other way in - typing "diag" - is in loop(), because it has to
         * work on a device with no card in the slot. */
        if (catnip_diag_marker_present()) {
            Serial.println("[catnip] diag: " CATNIP_DIAG_MARKER_PATH " is on the card");
            enter_diag();
            return;
        }
    }

    g_rt = catnip_rt_new_tracked(); /* Lua heap lives in PSRAM (#8) */
    if (!g_rt) {
        Serial.println("[catnip] FATAL: runtime allocation failed");
        return;
    }
    catnip_rt_set_log(g_rt, serial_log, nullptr);
    /* device/sensor/gpio/service/fs (#3), backed by the real drivers (#35).
     * What is wired and what is deliberately left as a no-op is listed at the
     * top of device/hal_meowkit.cpp. */
    catnip_api_open(g_rt, catnip_meowkit_hal_begin());

    g_shell = catnip_shell_new(g_rt, CATNIP_APPS_ROOT, host_now, host_pump, nullptr);
    if (!g_shell) {
        Serial.println("[catnip] FATAL: shell init failed");
        return;
    }
    /* Where a running app's ui.* tree becomes pixels (#30). The shell holds it
     * only so that a finished app's widgets are torn down through the same
     * vtable that built them; the pass itself belongs in loop(), next to the
     * display's own step. */
    g_be = catnip_lvgl_backend(g_rt);
    catnip_shell_set_backend(g_shell, g_be);

    /* The touch panel, for the pointer indev the input layer feeds LVGL (#31).
     * The switches are already up - the HAL began them - so this is the other
     * half. It shares the I2C bus that came up above. */
    catnip_ui_input_begin();

    /* The launcher menu (#33). Building it here is what takes the screen from
     * the boot animation: the first pass draws it, catnip_lvgl_backend_active()
     * turns true, and animate() stands down. */
    g_menu = catnip_menu_new(g_rt);
    rebuild_menu();

    /* Expect zero apps until the SD card is mounted (#32). */
    {
        /* Said apart, because they come from different places and one of them
         * is there whether or not a card is: a count "under /sd/..." that
         * included the built-ins would be a line that is not true. */
        int total = catnip_shell_count(g_shell);
        int builtin = 0;
        for (int i = 0; i < total; i++) {
            const catnip_app_entry *a = catnip_shell_app(g_shell, i);
            if (a && strncmp(a->dir, "builtin:", 8) == 0) builtin++;
        }
        Serial.printf("[catnip] shell ready, %d built-in app(s), %d under %s\n", builtin,
                      total - builtin, CATNIP_APPS_ROOT);
    }
}

void loop()
{
    /* Either side of the frame draw: a full-screen blit takes long enough that
     * a press landing during one would otherwise wait for it to finish. */
    poll_power_button();

    if (catnip_diag_active()) {
        /* The page polls the drivers it draws, so it is the whole loop. The
         * power button still works, because it arrives from the PMIC rather
         * than from any of the switches the page is testing. */
        catnip_diag_step();
        catnip_lvgl_step();
        poll_power_button();
        catnip_led_breathe();
        return;
    }

    /* Typed over USB, so it reaches a device with no card. Checked before the
     * frame draw so the request is not held up behind a blit. */
    if (catnip_diag_serial_request()) {
        Serial.println("[catnip] diag: asked for over serial");
        enter_diag();
        return;
    }

    /* The slot, before anything reads it. A card that arrived brings apps with
     * it and a card that left takes them away, and either way the list on
     * screen is wrong until it is rebuilt. */
    if (catnip_sd_poll()) {
        catnip_meowkit_hal_set_fs(catnip_sd_mounted());
        if (g_shell && catnip_shell_state(g_shell) != CATNIP_SHELL_RUNNING) {
            catnip_shell_refresh(g_shell);
            rebuild_menu();
        }
    }

    animate();
    poll_power_button();
    catnip_led_breathe();
    /* Sample the switches, the accelerometer and the battery once per pass, so
     * that no Lua call has to - see hal_meowkit.h. Before the shell steps, so a
     * script reads the device as it was this pass rather than last. */
    catnip_meowkit_hal_poll();

    /* Input into the event model (#31): the switches move a focus cursor over
     * the renderer's focus order and post prev/next/click to what is focused,
     * and the touch panel feeds the LVGL pointer indev so a tap activates the
     * button under it. Before the LVGL step, so the touch read the indev makes
     * there is fresh, and before the drain, so a press is delivered this same
     * frame. What it returns is what B asked for: nothing, back, or home. */
    int gesture = (g_rt && g_shell) ? catnip_ui_input_step(g_rt) : CATNIP_UI_GESTURE_NONE;

    /* A no-op until something brings LVGL up, which on this path is the first
     * widget the menu or an app draws (#30) - the boot animation goes straight
     * through the blit and never asks. It is called unconditionally because a
     * renderer that is only stepped on some passes through the loop is a stall
     * nobody can see the cause of. */
    catnip_lvgl_step();

    /* The order is catnip_render.h's, and its reasons are there: handlers run
     * in the drain and nowhere else, so they are outside LVGL's dispatch and on
     * the scheduler's watchdog. The menu's on_click latches its pick here; an
     * app's on_* run here too. */
    int delivered = g_rt ? catnip_render_drain(g_rt) : 0;

    /* The menu and a running app are the same kind of tree; which is up is the
     * shell's state, and the transitions between them are here (#33). */
    bool stepped = false;
    if (g_shell) {
        if (catnip_shell_state(g_shell) == CATNIP_SHELL_RUNNING) {
            int st = CATNIP_SHELL_RUNNING;
            stepped = true;
            /* Home first: a gesture that grew into a long press must not also
             * be read as the short one it passed through. */
            if (gesture == CATNIP_UI_GESTURE_HOME) {
                st = catnip_shell_home(g_shell);
            } else if (gesture == CATNIP_UI_GESTURE_BACK) {
                /* Here and not earlier: the drain above has just run the app's
                 * on_back, and this reads what it answered. The app may have
                 * climbed a level and stayed, in which case the step below still
                 * runs and nothing else happened. */
                st = catnip_shell_back(g_shell);
            }
            if (st == CATNIP_SHELL_RUNNING) st = catnip_shell_step(g_shell);
            /* Whichever way it ended - B, home, finished, faulted - teardown
             * loaded the blank screen, so the menu has to be rebuilt before the
             * next pass draws it. */
            if (st == CATNIP_SHELL_MENU) rebuild_menu();
        } else {
            /* In the menu. A click has latched which app to launch; the launch
             * tears the menu tree down, so one that then fails to load must put
             * the menu back rather than leave a blank screen. */
            const char *id = catnip_menu_take_pick(g_menu);
            if (id) {
                char err[64];
                if (catnip_shell_launch_id(g_shell, id, err, sizeof(err)) != 0) {
                    Serial.printf("[catnip] menu: %s could not launch: %s\n", id, err);
                    rebuild_menu();
                }
            }
        }
    }

    if (g_rt && g_be) catnip_render(g_rt, g_be);

    /* The frame, last: the counter it draws is read off the tree the pass above
     * has just reconciled, so it can never show the previous frame's numbers.
     * It is hidden until there is a screen to wrap - a bar over a black panel
     * would be the only thing on it. */
    update_status(false);
    catnip_frame_show(catnip_lvgl_backend_active());
    /* Running an app, the shell answers what the header reads - a title the app
     * set, else its manifest name - and only after app code could have run,
     * since that is the only thing that can change the answer and the question
     * costs a Lua call. In the menu the carousel answers: its cells are
     * pictures with no captions, so the bar is the only place a name can be. */
    if (g_shell) {
        if (catnip_shell_state(g_shell) == CATNIP_SHELL_RUNNING) {
            if (delivered || stepped) catnip_frame_set_title(catnip_shell_title(g_shell));
        } else if (g_menu) {
            catnip_frame_set_title(catnip_menu_focus_name(g_menu));
        }
    }
    catnip_frame_step(g_rt);
}
