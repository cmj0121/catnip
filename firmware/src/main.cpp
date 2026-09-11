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
#include "catnip_pages.h"
#include "catnip_device_info.h"
#include "generated/version.h"
#include "catnip_settings.h"
#include "catnip_runtime.h"
#include "catnip_shell.h"
#include "catnip_bar.h"
#include "catnip_busy.h"
#include "catnip_icon_map.h"
#include "catnip_ui.h"
#include "device/board.h"
#include "device/diag.h"
#include "device/display.h"
#include "device/i2cbus.h"
#include "device/input.h"
#include "device/press_gesture.h"
#include "device/ioexp.h"
#include "device/led.h"
#include "device/hal_meowkit.h"
#include "device/app_icon.h"
#include "device/frame.h"
#include "device/ble.h"
#include "device/ble_hid.h"
#include "device/ble_adv.h"
#include "device/lvgl_backend.h"
#include "device/lvgl_port.h"
#include "device/pmu.h"
#include "device/sd_mount.h"
#include "device/power.h"
#include "device/i2cbus.h"
#include "device/prefs.h"
#include "device/rtc.h"
#include "device/toast.h"
#include "device/rtc_time.h"
#include "device/ui_input.h"
#include "device/ui_input_core.h"
#include "device/net_time.h"
#include "device/wifi.h"
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
/* The three screens the platform owns - the launcher, the device page and the
 * preference page - and the rules for moving between them (#81). They are a
 * mode of the launcher rather than a state of the shell: the shell's states are
 * about running an app, and none of these runs anything.
 *
 * In catnip_pages.c with no Arduino in it, so a host test can press buttons at
 * them; what is left here is the board they ask for what a chip is called, what
 * the clock says, and what a brightness does. */
static catnip_pages *g_pages;
/* The settings as they now stand. The one copy the rest of this file reads. */
static catnip_config g_cfg;

/* A Lua fault, on the glass as well as in the log. The log keeps the traceback;
 * this is the one line somebody holding the device needs to see, because the
 * alternative is a screen that stopped responding and said nothing. */
static void lua_error_toast(void *ud, const char *msg, size_t len)
{
    (void)ud;
    (void)len;
    catnip_toast_show(msg);
}

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
/* B's press timer while the diagnostic page is up. Separate from ui_input's,
 * which is not running then. */
static catnip_press g_press_diag;

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
    /* And the hint with it: the loop's diag branch returns before the line that
     * would hide it, so a hint left up here would float over a page whose whole
     * job is to show what the panel is doing. */
    catnip_frame_show_hint(false);
    /* Hand the touch panel back before the page takes it: the input layer's LVGL
     * pointer indev would otherwise keep injecting taps onto the page's own
     * screen (#31/#42). Safe on the boot-marker path, where no indev was made. */
    catnip_ui_input_end();
    g_animating = false;
    if (!catnip_diag_begin()) g_animating = true;
}

/* And out of it, back to the cat. The page hands the screen back; what is on
 * the other side is whatever was there before it - which is the launcher,
 * because entering tore any running app down. The touch panel comes back with
 * it: enter_diag() gave the indev away so the page's own screen would not be
 * taking taps, and nothing else puts it back. */
static void leave_diag(void)
{
    if (!catnip_diag_active()) return;
    catnip_diag_end();
    catnip_ui_input_begin();
    catnip_pages_rebuild(g_pages);
}

/* The splash, and the busy ring under it.
 *
 * It was a four-frame paw cycle, and the cycle is gone: the device has one
 * picture of working now and this is where it is first shown. Three things were
 * saying "wait" in three different shapes - a paw waving at boot, a label the
 * WiFi prober refreshed itself, and nothing at all while an app loaded - and
 * only the ring is a picture of waiting rather than a picture of a cat.
 *
 * The splash stays as the ground because it is the only thing on the panel that
 * says which device this is, and the ring is drawn over it. Frames from the
 * owner's card still play as the ground if they supplied any; the ring rides on
 * top of whichever it is, so the indicator is the same either way. */
static const uint16_t *const g_anim_frames[] = {catnip_splash};
static const size_t g_anim_count = sizeof(g_anim_frames) / sizeof(g_anim_frames[0]);
static unsigned long g_frame_ms = 625;

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

/* The ring, straight onto the panel, over whatever the ground is. Eight small
 * circles rather than a frame: 153,600 bytes to move eight dots was what the
 * paw cycle cost, and the ground under them does not change. */
static void draw_busy_ring(unsigned now)
{
    /* The same ring at the same size as the one LVGL draws - it is one
     * animation, and two sizes of it would be two. Low on the panel rather than
     * centred, because here the ground is the splash and the cat is what the
     * middle is for; the LVGL one has an empty screen to sit in the middle of. */
    static const int kCx = CATNIP_SCREEN_W / 2;
    static const int kCy = CATNIP_SCREEN_H - 40;
    static const int kR = 26;
    catnip_busy_dot dots[CATNIP_BUSY_DOTS];
    int n =
        catnip_busy_dots(catnip_busy_phase(now), kCx, kCy, kR, dots, CATNIP_BUSY_DOTS);

    for (int i = 0; i < n; i++) {
        /* The tail is drawn in the ground's own colour so a dot that has faded
         * out is gone rather than dark: there is no alpha on a direct blit, and
         * a ring of grey circles on the splash would be a ring of holes in it.
         *
         * The five steps are the ink stepped towards the ground, worked out
         * once here rather than blended per pixel. */
        static const uint16_t kInk[CATNIP_BUSY_DOTS] = {
            0x1082, 0x2103, 0x3184, 0x4A26, 0x6AC8, 0x9BAA, 0xCCEC, 0xDD25,
        };
        catnip_display_dot(dots[i].x, dots[i].y, 5, kInk[dots[i].level]);
    }
}

static void animate(void)
{
    static unsigned long last = 0;
    static unsigned last_phase = (unsigned)-1;
    unsigned long now = millis();
    /* g_animating is deliberately not cleared when the renderer takes over: it
     * is what set_screen() restores when the panel comes back on, and clearing
     * it would make the handover depend on whether the screen happened to be
     * off at the moment it occurred. Who owns the panel is asked afresh here
     * every pass instead. */
    if (catnip_lvgl_backend_active()) return;
    if (!g_animating) return;
    /* The ground, when there is more than one of it to play. */
    if (frame_count() > 1 && now - last >= g_frame_ms) {
        last = now;
        g_frame = (g_frame + 1) % frame_count();
        show_frame(g_frame);
        last_phase = (unsigned)-1; /* the blit took the ring with it */
    }
    /* And the ring, on its own cadence. */
    {
        unsigned phase = catnip_busy_phase((unsigned)now);
        if (phase != last_phase) {
            last_phase = phase;
            draw_busy_ring((unsigned)now);
        }
    }
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
    /* The owner's brightness, not full: this is the one place the panel is lit
     * after boot, so it is the one place that has to remember the setting. */
    catnip_display_backlight(on ? (uint8_t)(g_cfg.screen_brightness * 255 / 100) : 0);
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

/* The power button, asked of the PMIC over I2C.
 *
 * On a cadence, because the loop runs at several hundred passes a second and
 * two register reads at 100 kHz cost about 450 us of every one of them - a
 * fifth of the whole pass spent asking a button that a human presses at most
 * twice a second whether it has moved. Twenty times a second is far inside what
 * anyone can tell apart on a press, and the PMIC latches the event rather than
 * reporting a level, so nothing is missed between asks.
 *
 * `force` is for the paths that must not wait for the cadence: the diagnostic
 * page is the whole loop while it is up, and the boot animation runs before
 * there is a loop at all. */
static unsigned g_pmu_last;
static void poll_power_button_at(unsigned now, bool force)
{
    if (!force && (unsigned)(now - g_pmu_last) < 50u) return;
    g_pmu_last = now;
    if (catnip_pmu_power_key_held()) power_off();
    if (catnip_pmu_power_key_pressed()) set_screen(!g_screen_on);
}

static void poll_power_button(void)
{
    poll_power_button_at((unsigned)millis(), false);
}

/* Read the owner's settings and act on them. Where they come from and which
 * copy wins is device/prefs.h; everything there is optional, so a device with
 * no card and nothing saved still boots looking like itself. */

/* The settings that hardware has to be told about, told to it. Called at boot
 * and again on every step the owner makes on the settings page, which is what
 * makes a brightness visible while it is being chosen rather than after. */
static void apply_settings(const catnip_config *cfg)
{
    catnip_led_configure((uint8_t)(cfg->led_brightness * 255 / 100),
                         cfg->led_breaths_per_second);
    if (g_screen_on)
        catnip_display_backlight((uint8_t)(cfg->screen_brightness * 255 / 100));
}

static void apply_config(void)
{
    catnip_config cfg;
    catnip_prefs_load(&cfg);
    g_cfg = cfg;

    apply_settings(&cfg);
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

/* Join the network named in the config, and set the clock from it. Does
 * nothing when no network is configured - which is a device that never brings
 * the radio up, most devices most of the time. The clock sync brings the radio
 * up and leaves it up (see net_time.cpp), so this both connects and, if the
 * server answers, corrects the time. */
static void maybe_join_network(void)
{
    if (!g_cfg.wifi_ssid[0]) return;
    Serial.printf("[catnip] wifi: '%s' configured, connecting\n", g_cfg.wifi_ssid);
    catnip_net_time_sync(g_cfg.wifi_ssid, g_cfg.wifi_psk, g_cfg.tz_offset_min);
}

/* Raise the backlight gradually - an abrupt jump to full reads as a flash. */
static void fade_in(void)
{
    int target = g_cfg.screen_brightness * 255 / 100;
    for (int level = 0; level <= target; level += 5) {
        catnip_display_backlight((uint8_t)level);
        delay(4);
    }
    /* The last step of the loop lands below the target whenever it is not a
     * multiple of five, and a backlight one step short of what was asked for is
     * a setting that never quite takes. */
    catnip_display_backlight((uint8_t)target);
}

/* Put the battery on the menu's status line. Wifi is deliberately not shown:
 * the MeowKit HAL leaves wifi_status unwired (see hal_meowkit.cpp), so an
 * indicator would read "disconnected" forever rather than the truth, and an
 * honest gap beats a fake reading - a lesson this project has paid for before.
 * Throttled to once every couple of seconds, because it runs Lua to reach the
 * label and the charge barely moves; `force` refreshes it the moment the menu
 * is rebuilt so the line is never briefly blank. */
static unsigned long g_status_last;

/* ---- what the pages ask of the board ------------------------------------
 *
 * Eight small functions, and every one of them is a question catnip_pages.c
 * must not be able to answer: what a chip is called, what is on the bus, what a
 * brightness does, where a setting is written down. That file owns the rules;
 * this one owns the device. */

/* What this device is, written out. Asked afresh every time the page opens
 * rather than kept: half of it moves - the battery, the free heap, what is
 * answering on the bus - and a page of facts that were true a while ago is
 * worse than no page at all. */
static int env_info_rows(void *ud, char (*rows)[CATNIP_INFO_ROW_MAX], int max)
{
    uint8_t addrs[8];
    int n = 0;
    int i;

    (void)ud;
    if (max < 12) return 0;

    /* The order is what a reader can see without scrolling, and the page shows
     * only its first few lines: what changes and what gets checked goes first -
     * the version, then the clock, the battery and the card. The chip revision
     * and the MAC are looked *up*, once, by somebody who came here on purpose,
     * and they can be scrolled to.
     *
     * This is not cosmetic. The clock's line was seventh, which on a page that
     * shows two and a half of eleven meant the one fact somebody had come to
     * check was off the bottom of a screen that gave no sign there was more. */
    snprintf(rows[n++], CATNIP_INFO_ROW_MAX, "catnip %s", CATNIP_VERSION);
    {
        uint32_t t = catnip_rtc_now();
        if (t) {
            int32_t y;
            uint32_t mo, d, h, mi;
            catnip_rtc_split(t, &y, &mo, &d, &h, &mi, NULL);
            snprintf(rows[n++], CATNIP_INFO_ROW_MAX, "clock %04d-%02u-%02u %02u:%02u",
                     (int)y, (unsigned)mo, (unsigned)d, (unsigned)h, (unsigned)mi);
        } else {
            /* Which part, even when it has no time to give: knowing the chip is
             * there and unset is a different problem from it not being there. */
            snprintf(rows[n++], CATNIP_INFO_ROW_MAX, "clock not set (%s)",
                     catnip_rtc_part());
        }
    }
    snprintf(rows[n++], CATNIP_INFO_ROW_MAX, "battery %d%%",
             catnip_pmu_battery_percent());
    if (catnip_sd_mounted())
        snprintf(rows[n++], CATNIP_INFO_ROW_MAX, "card %llu MB",
                 (unsigned long long)(SD_MMC.cardSize() / (1024ULL * 1024ULL)));
    else snprintf(rows[n++], CATNIP_INFO_ROW_MAX, "card none");
    snprintf(rows[n++], CATNIP_INFO_ROW_MAX, "built %s", CATNIP_BUILD_DATE);
    snprintf(rows[n++], CATNIP_INFO_ROW_MAX, "%s rev %d, %d MHz", ESP.getChipModel(),
             ESP.getChipRevision(), (int)ESP.getCpuFreqMHz());
    snprintf(rows[n++], CATNIP_INFO_ROW_MAX, "flash %u MB",
             (unsigned)(ESP.getFlashChipSize() / (1024U * 1024U)));
    snprintf(rows[n++], CATNIP_INFO_ROW_MAX, "psram %u KB free of %u MB",
             (unsigned)(ESP.getFreePsram() / 1024U),
             (unsigned)(ESP.getPsramSize() / (1024U * 1024U)));
    snprintf(rows[n++], CATNIP_INFO_ROW_MAX, "heap %u KB free",
             (unsigned)(ESP.getFreeHeap() / 1024U));

    {
        uint64_t mac = ESP.getEfuseMac();
        /* getEfuseMac() returns the six bytes least-significant first, which is
         * the reverse of how a MAC is written down. */
        snprintf(rows[n++], CATNIP_INFO_ROW_MAX, "mac %02X:%02X:%02X:%02X:%02X:%02X",
                 (unsigned)(mac & 0xFF), (unsigned)((mac >> 8) & 0xFF),
                 (unsigned)((mac >> 16) & 0xFF), (unsigned)((mac >> 24) & 0xFF),
                 (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
    }

    {
        int found = catnip_i2c_present(addrs, (int)(sizeof(addrs) / sizeof(addrs[0])));
        int shown = found < (int)(sizeof(addrs) / sizeof(addrs[0]))
                        ? found
                        : (int)(sizeof(addrs) / sizeof(addrs[0]));
        int off = snprintf(rows[n], CATNIP_INFO_ROW_MAX, "i2c");
        for (i = 0; i < shown && off > 0 && off < CATNIP_INFO_ROW_MAX; i++)
            off += snprintf(rows[n] + off, (size_t)(CATNIP_INFO_ROW_MAX - off), " %02X",
                            addrs[i]);
        if (!found) snprintf(rows[n], CATNIP_INFO_ROW_MAX, "i2c nothing responded");
        n++;
    }
    return n;
}

static void env_apply(void *ud, const catnip_config *cfg)
{
    (void)ud;
    apply_settings(cfg);
}

static void env_save(void *ud, const catnip_config *cfg)
{
    (void)ud;
    catnip_prefs_save(cfg);
}

static void env_title(void *ud, const char *title)
{
    (void)ud;
    catnip_frame_set_title(title);
}

static void env_icons_load(void *ud, const catnip_app_entry *apps, int n)
{
    (void)ud;
    catnip_app_icons_load(apps, n);
}

static bool env_card_present(void *ud)
{
    (void)ud;
    return catnip_sd_mounted();
}

static uint32_t env_now_epoch(void *ud)
{
    (void)ud;
    return catnip_rtc_now();
}

static void env_enter_diag(void *ud)
{
    (void)ud;
    enter_diag();
}

static const catnip_pages_env kPagesEnv = {
    env_info_rows,    env_apply,     env_save,       env_title, env_icons_load,
    env_card_present, env_now_epoch, env_enter_diag, nullptr,
};

/* The battery in the frame's bar. Every two seconds rather than every pass: the
 * gauge does not move faster than that and each write invalidates the area.
 * The launcher's own live cells (#75) ride along on the same cadence, for the
 * same reason: a clock that changes once a minute has no business on the I2C
 * bus thirty times a second. */
static void update_status(bool force)
{
    unsigned long now = millis();
    if (!force && now - g_status_last < 2000) return;
    g_status_last = now;
    catnip_frame_set_battery(catnip_pmu_battery_percent());
    catnip_pages_glance(g_pages);
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
    /* After the scan, so the dump it prints sits under the list of what
     * answered. It takes a second, deliberately: identifying the part means
     * watching a register tick, and there is no shorter way to do that. */
    catnip_rtc_begin();

    /* Before the panel is lit, and before the card is even looked for: the
     * fade below climbs to the owner's brightness, and with an empty g_cfg that
     * target is zero - a boot that ends in a black screen with nothing wrong.
     * The real settings arrive a few lines down and correct this. */
    catnip_config_defaults(&g_cfg);

    if (catnip_display_begin()) {
        catnip_display_blit(catnip_splash);

        fade_in();
        Serial.println("[catnip] display up, splash shown");
    } else {
        Serial.println("[catnip] display init FAILED");
    }

    /* After the splash, deliberately: the card is the slowest thing in the
     * boot and nothing on screen should wait for it. */
    bool card = catnip_sd_mount();
    /* Outside the card check on purpose: NVS is where a setting lives on a
     * device with no card, and that is most devices most of the time. Reading
     * the owner's settings only when a card happened to be in the slot is the
     * exact failure this page exists to fix. */
    apply_config();
    g_rt = catnip_rt_new_tracked(); /* Lua heap lives in PSRAM (#8) */
    if (!g_rt) {
        Serial.println("[catnip] FATAL: runtime allocation failed");
        return;
    }
    catnip_rt_set_log(g_rt, serial_log, nullptr);
    catnip_rt_set_error(g_rt, lua_error_toast, nullptr);
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
    g_pages = catnip_pages_new(g_rt, g_shell, &g_cfg, &kPagesEnv);
    if (!g_pages) {
        Serial.println("[catnip] FATAL: platform screens could not be built");
        return;
    }
    catnip_pages_rebuild(g_pages);

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

    /* The marker file means the owner wants the input page on this boot. Last,
     * after the shell is standing, rather than instead of it: long B leaves the
     * page now, and the way out has to lead to a launcher that exists. It cost
     * nothing to build - the page takes the screen from it on the next line -
     * and the alternative is a device whose diagnostic exits to nothing.
     *
     * The other way in - typing "diag" - is in loop(), because it has to work
     * on a device with no card in the slot. */
    if (card && catnip_diag_marker_present()) {
        Serial.println("[catnip] diag: " CATNIP_DIAG_MARKER_PATH " is on the card");
        enter_diag();
        return;
    }

    /* Join the configured network, if there is one, and set the clock from it
     * (#82/#84). At boot and again whenever a card is inserted - see the card
     * poll in loop(). The radio stays up once joined, so the header shows it. */
    maybe_join_network();
}

/* What the device is waiting for, or NULL when it is not waiting.
 *
 * Asked afresh every pass rather than pushed, because the answer is a state
 * somebody else owns and a flag set at the start of one is a flag that outlives
 * it the day the other end returns early.
 *
 * Loading an app is not on this list because it does not last a pass: the
 * launch reads Lua off the card and runs it without returning, so there is no
 * pass in the middle of it to draw anything from. It puts the ring up itself,
 * on the way in - see below. */
static const char *busy_reason(void)
{
    /* Either radio, one word. A page that looks at more than one of them is
     * looking for the same thing on each, and "scanning for Wi-Fi" followed by
     * "scanning for BLE" would be the device narrating its own implementation
     * at somebody who asked what is nearby. */
    if (catnip_wifi_scanning() || catnip_ble_scanning()) return "scanning";
    return NULL;
}

/* Waiting, said in both places at once.
 *
 * The ring is on the screen and the LED is not, and that is the point of saying
 * it twice: the two things this is raised for are a scan and an app being
 * loaded, and loading an app is the one moment the screen is about to be
 * replaced anyway. One call, so the two can never disagree about whether the
 * device is working. */
static void say_busy(const char *what)
{
    catnip_frame_set_busy(what);
    catnip_led_busy(what != NULL);
}

/* Long A's answer, turned into a bar.
 *
 * The app was asked which of its actions apply and answered with ids out of its
 * own manifest; this is where those become two or three buttons. It runs after
 * the drain, because the handler that answers runs in the drain - and it runs
 * every pass, because the answer is latched and reading it is what clears it.
 *
 * An app that answered with nothing gets no bar, which is the ordinary case for
 * a page whose long press does the thing itself - the grid pins an app rather
 * than offering to. */
static void offer_actions(void)
{
    const catnip_action *catalogue;
    int n_catalogue = 0;
    int index = CATNIP_INDEX_NONE;
    catnip_handle owner;

    if (!g_rt) return;
    /* The running app's, or the platform page's when nothing is running. Two
     * sources and one bar: the launcher's own pages declare their actions in C
     * where an app declares them in JSON, and the thing that draws them cannot
     * tell the difference - which is the point. The launcher may offer no
     * operation an app could not have offered the same way. */
    catalogue = catnip_shell_actions(g_shell, &n_catalogue);
    if (!catalogue) catalogue = catnip_pages_actions(g_pages, &n_catalogue);
    owner = catnip_ui_input_options_asked(&index);
    (void)catnip_bar_offer(catnip_ui_input_bar(), g_rt, catalogue, n_catalogue, owner,
                           index);
}

/* And what it looks like. Names and glyphs are read off the bar every pass; the
 * frame's own guard is what keeps that from repainting a panel that has not
 * changed. */
static void draw_actions(void)
{
    const catnip_bar *bar = catnip_ui_input_bar();
    const char *names[CATNIP_BAR_CELLS];
    catnip_icon icons[CATNIP_BAR_CELLS];
    int n;

    if (!catnip_bar_up(bar)) {
        catnip_frame_set_actions(nullptr, nullptr, 0, 0, false);
        return;
    }
    n = bar->n > CATNIP_BAR_CELLS ? CATNIP_BAR_CELLS : bar->n;
    for (int i = 0; i < n; i++) {
        names[i] = bar->items[i].name;
        icons[i] = catnip_icon_from_name(bar->items[i].icon);
    }
    /* Whether it steps is the bar's to say, not a thing to re-derive from the
     * count: two actions whose second is destructive step as well, because B
     * will not carry that one. */
    catnip_frame_set_actions(names, icons, n, bar->focus, catnip_bar_modal(bar));
}

/* Whether the panel belongs to the running app right now.
 *
 * Two answers, in order: the visible screen's own `frame`, and the manifest's
 * when the screen said nothing. That order is the whole point - `frame` used to
 * be the manifest's alone, which made "the whole panel is mine" a claim an app
 * made once for every screen it would ever show, and the clock is the app that
 * cannot make it: its face wants the panel and its setter wants the bar back.
 *
 * In the menu neither answers yes: no screen there sets `frame`, and the shell
 * reports bare only while an app is actually running.
 *
 * Asked of Lua, so asked only when the answer can have moved.
 *
 * It is one pcall into the runtime, which is about 210 us on this chip - a
 * sixth of a pass spent asking a question whose answer changes when an app
 * pushes a screen and at no other time. A screen can only appear or vanish
 * through a handler (the drain), a page or shell transition, or an app's own
 * chunk being stepped; `moved` is those three, and it is worked out before this
 * is called because the region has to be right before the tree is drawn. */
static bool app_is_bare(bool moved)
{
    static bool cached;
    static bool asked;

    if (!g_rt) return false;
    if (moved || !asked) {
        cached = catnip_ui_bare(g_rt, catnip_shell_bare(g_shell) != 0);
        asked = true;
    }
    return cached;
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
        /* Forced: the diagnostic page is the whole loop, and its passes are
         * slow enough that a cadence would be the only thing reading the
         * button. */
        poll_power_button_at((unsigned)millis(), true);
        catnip_led_breathe();
        /* Holding B is the way out, and it goes back to the cat rather than
         * through a restart: the page borrowed lv_screen_active() and gives it
         * back, so there is a shell on the other side of the gesture.
         *
         * Long B rather than a button on the page, because long B is already
         * "home, and the platform's alone" everywhere else. It is read here and
         * not in diag.cpp so that the page keeps knowing nothing about what is
         * above it; the switch it is reading is one it also draws, which is its
         * own confirmation that the press registered. */
        if (catnip_press_step(&g_press_diag, catnip_input_down(CATNIP_BTN_B),
                              (unsigned)millis()) == CATNIP_PRESS_LONG)
            leave_diag();
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
    /* And the slot, on a slower one still. A card is put in by hand and the
     * answer costs a mount check; four times a second is faster than anyone can
     * push one in and let go, and it was being asked four hundred and sixty
     * times a second. */
    static unsigned sd_last;
    unsigned now_ms = (unsigned)millis();
    bool ask_sd = (unsigned)(now_ms - sd_last) >= 250u;
    if (ask_sd) sd_last = now_ms;
    if (ask_sd && catnip_sd_poll()) {
        bool mounted = catnip_sd_mounted();
        catnip_meowkit_hal_set_fs(mounted);
        if (g_shell && catnip_shell_state(g_shell) != CATNIP_SHELL_RUNNING) {
            catnip_shell_refresh(g_shell);
            catnip_pages_rebuild(g_pages);
        }
        /* A card that just arrived brings the owner's settings with it - the
         * network among them (#82). Re-read them and act on the network, so a
         * Wi-Fi card plugged into a running device joins without a reboot,
         * which is the whole point of putting it on the card. */
        if (mounted) {
            catnip_config cfg;
            catnip_prefs_load(&cfg);
            g_cfg = cfg;
            apply_settings(&g_cfg);
            maybe_join_network();
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
    catnip_page page_was = catnip_pages_current(g_pages);
    /* The platform's own screens, before the shell's states: none of them is
     * one. While one is up the menu's pick is not read and no app can start,
     * because the tree on screen is that page's and there is nothing on it to
     * launch.
     *
     * The rules are catnip_pages.c's; what is left here is the shell, which is
     * the one thing on this path that genuinely needs the board - it launches
     * code off a card and tears it down through the backend. */
    if (catnip_pages_step(g_pages, gesture)) {
        /* A page was entered or left, and the tree on screen is now a different
         * one. Nothing else this pass, so the render below draws it whole. */
    } else if (g_shell && catnip_shell_state(g_shell) == CATNIP_SHELL_RUNNING) {
        int st = CATNIP_SHELL_RUNNING;
        stepped = true;
        /* Home first: a gesture that grew into a long press must not also be
         * read as the short one it passed through. */
        if (gesture == CATNIP_UI_GESTURE_HOME) {
            /* Home is the cat, so the ring opens there rather than where this
             * app was launched from. Short B is the one that returns you to
             * where you came from. */
            catnip_menu_home(catnip_pages_menu(g_pages));
            st = catnip_shell_home(g_shell);
        } else if (gesture == CATNIP_UI_GESTURE_BACK) {
            /* Here and not earlier: the drain above has just run the app's
             * on_back, and this reads what it answered. The app may have climbed
             * a level and stayed, in which case the step below still runs and
             * nothing else happened. */
            st = catnip_shell_back(g_shell);
        }
        if (st == CATNIP_SHELL_RUNNING) st = catnip_shell_step(g_shell);
        /* Whichever way it ended - B, home, finished, faulted - teardown loaded
         * the blank screen, so the menu has to be rebuilt before the next pass
         * draws it. */
        if (st == CATNIP_SHELL_MENU) catnip_pages_rebuild(g_pages);
    } else if (g_shell) {
        /* In the menu. A click has latched which app to launch; the launch
         * tears the menu tree down, so one that then fails to load must put the
         * menu back rather than leave a blank screen. */
        const char *id = catnip_pages_take_launch(g_pages);
        if (id) {
            char err[64];
            /* The ring, and then straight onto the panel before the launch
             * blocks: reading an app off the card and running its chunk takes
             * long enough to be noticed and does not return until it is done,
             * so there is no later pass to draw from. It does not turn while it
             * is up, and that is honest - nothing is happening in this device
             * except the thing it is saying. */
            say_busy("loading");
            catnip_lvgl_step();
            if (catnip_shell_launch_id(g_shell, id, err, sizeof(err)) != 0) {
                Serial.printf("[catnip] menu: %s could not launch: %s\n", id, err);
                catnip_pages_rebuild(g_pages);
            }
            say_busy(NULL);
        }
    }

    /* Whether the panel belongs to the app, decided fresh every pass and before
     * the tree is drawn - it is what says whether the region reserves room for
     * a bar. Asked of the visible screen first and of the manifest only when
     * the screen said nothing, so an app whose screens are all one shape still
     * declares it once and the clock can hand the bar back for its setter. */
    /* Everything that can have moved the tree since the last pass drew it. */
    const bool moved =
        delivered > 0 || stepped || catnip_pages_current(g_pages) != page_was;
    const bool bare = app_is_bare(moved);
    catnip_lvgl_backend_set_bare(bare);

    /* After the drain that ran the app's handlers and before the tree is drawn,
     * because a bar put up now is a bar the user sees this frame. */
    offer_actions();

    /* How many objects the pass touched, which is the honest answer to "did
     * anything on screen change" - and the only one, since an app writes a
     * property without telling anybody. */
    const int drawn = (g_rt && g_be) ? catnip_render(g_rt, g_be) : 0;

    /* Carry any clock sync forward (#84): it joins the network, asks the time
     * and writes the RTC over several passes, dropping the radio when it is
     * done. Cheap when idle - it returns at once unless a sync is in flight. */
    /* The radio, once a pass: it steps a join in flight. Before this it was
     * only stepped from inside the clock sync, so a join was advanced only when
     * an NTP request happened to be in flight too. */
    (void)catnip_wifi_poll();
    catnip_ble_poll();

    /* Nothing is running, so nothing is a mouse (#59) or a beacon (#60).
     *
     * catnip_ble_hid_begin() and catnip_ble_adv_begin() each take the stack and
     * park the Wi-Fi link, and the app that called it is the only thing that
     * knows to give them back - which means an app that leaves any way other
     * than the one it planned for (short B, long B, a Lua error, a chunk that
     * simply ends) would leave the device advertising for ever, never rejoining
     * a network, with nothing on screen to say why.
     *
     * An invariant rather than a hook on the exit path: there are four ways out
     * of a running app and this covers all of them, including the ones added
     * later. Both end() calls are a no-op when nothing is up, so this costs a
     * comparison per pass - and it is the same rule for both radios because the
     * hazard is the same one, a peripheral surface outliving the app that meant
     * it. */
    if (!g_shell || catnip_shell_state(g_shell) != CATNIP_SHELL_RUNNING) {
        if (catnip_ble_hid_up()) catnip_ble_hid_end();
        if (catnip_ble_adv_up()) catnip_ble_adv_end();
    }

    catnip_net_time_poll();

    /* The frame, last: the counter it draws is read off the tree the pass above
     * has just reconciled, so it can never show the previous frame's numbers.
     * It is hidden until there is a screen to wrap - a bar over a black panel
     * would be the only thing on it. */
    update_status(false);
    /* Not over a canvas. `frame: "bare"` is a promise about the whole panel, and
     * a bar floating on the top layer would be the platform breaking it. */
    catnip_frame_show(catnip_lvgl_backend_active() && !bare);
    /* And what the four directions do from where the ring is (#80). Derived
     * from the tree by the same function the input pass asks, so the arrow that
     * is lit and the press that does something cannot disagree. */
    /* Unless the app asked for the panel to itself, which is `frame: "bare"`,
     * or asked for the hint not to be drawn, which is `"hints": false`. The
     * first is a claim about the whole surface and the second about this app's
     * directions needing no explanation, and either is reason enough. */
    catnip_frame_show_hint(catnip_lvgl_backend_active() && !bare &&
                           catnip_shell_hints(g_shell));
    /* The hint and the counter are both read out of the tree, and both cost a
     * walk of it. What they say changes when the tree changes or when the ring
     * moves, and on a device sitting still neither does - so both are asked
     * only then, and the answers stand until something moves.
     *
     * The drawing behind them is guarded too, and separately: that guard is
     * about not repainting, this one is about not asking. */
    {
        static catnip_handle focus_was = CATNIP_HANDLE_NONE;
        static bool asked;
        catnip_handle focus_now = catnip_ui_input_focused();

        if (moved || drawn || focus_now != focus_was || !asked) {
            focus_was = focus_now;
            asked = true;
            catnip_frame_set_hint(catnip_ui_input_hint(g_rt, focus_now));
            catnip_frame_step(g_rt);
        }
    }
    /* And the bar of actions, if one is up. It is drawn after the hint because
     * putting it up lifts the hint onto its shoulder, and the hint has to exist
     * to be lifted. */
    draw_actions();
    /* The status strip in the bar (#83): a card when one is in the slot, the
     * radio when it is up, and the sync glyph only while a sync is actually in
     * flight - a badge that was always there would be saying "this device has
     * networking", which is not information. */
    catnip_frame_set_status(catnip_sd_mounted(),
                            catnip_wifi_status() == CATNIP_WIFI_CONNECTED,
                            catnip_net_time_busy());
    /* Running an app, the shell answers what the header reads - a title the app
     * set, else its manifest name - and only after app code could have run,
     * since that is the only thing that can change the answer and the question
     * costs a Lua call. In the menu the carousel answers: its cells are
     * pictures with no captions, so the bar is the only place a name can be. */
    if (g_shell) {
        if (catnip_shell_state(g_shell) == CATNIP_SHELL_RUNNING) {
            if (delivered || stepped) catnip_frame_set_title(catnip_shell_title(g_shell));
        } else if (catnip_pages_current(g_pages) == CATNIP_PAGE_HOME) {
            catnip_frame_set_title(catnip_menu_focus_name(catnip_pages_menu(g_pages)));
        }
        /* The other two name themselves as they are entered, in
         * catnip_pages.c - they are one screen each and their titles do not
         * change while they are up, where the ring's does on every step. */
    }
    /* The one picture of working there is, and the loop is what knows when to
     * show it: a scan with nothing to show yet, or an app being loaded off the
     * card. Neither of those is drawn by whoever is waiting - what waiting
     * looks like is the platform's, exactly as the bar and the hint are. */
    say_busy(busy_reason());
    catnip_toast_step();

    /* And give the rest of the system the pass back when this one did nothing.
     *
     * A loop with no yield in it runs as fast as the CPU will go - about nine
     * hundred passes a second here - and every one of those passes is time the
     * idle task does not get. FreeRTOS uses the idle task for its own
     * housekeeping and the SoC uses it to clock down, so a busy loop is not
     * only a flat battery: it is the one shape that starves the scheduler on a
     * device that is also running a radio.
     *
     * One millisecond, and only when the pass found nothing to do. That is a
     * ceiling of about five hundred passes a second, which is still two orders
     * of magnitude faster than a thumb - and the moment anything happens, the
     * next pass is immediate again, so the yield can never be in the way of a
     * press it has already seen. */
    if (!delivered && !stepped && !drawn && gesture == CATNIP_UI_GESTURE_NONE) delay(1);
}
