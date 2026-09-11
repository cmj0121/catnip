/*
 * hal_meowkit.cpp - see hal_meowkit.h. Issue #35.
 *
 * This file is a wiring diagram and nothing else. Every hook below is a few
 * lines that turn a Lua-shaped request into a call on a driver in this
 * directory; anything that needed thinking about belongs in the driver, and if
 * a hook here starts growing logic that is the sign it went to the wrong place.
 *
 * What is live: the status LED, the battery, the backlight, the seven switches,
 * the accelerometer and the SD card.
 *
 * What is deliberately not, and why - because a reader looking for a missing
 * hook will look here first, and "it is not in the table" does not say whether
 * that was a decision or an oversight:
 *
 *   device.vibrate  There is no driver for the motor. Nothing on this board has
 *                   yet driven it, so there is no measured pin to call, and a
 *                   hook that wrote to a guessed one would either do nothing or
 *                   do something to a pin belonging to something else.
 *
 *   sensor.rtc      Something answers at 0x51, which is the conventional
 *                   address for a PCF8563 - and that is the whole of what is
 *                   known. No register on it has ever been read. On this board
 *                   the vendor's documentation has now been wrong three times
 *                   (the display's chip-select, all three button pins, the
 *                   IMU's part number), so the part at 0x51 is a guess until a
 *                   register says otherwise, and a driver written against a
 *                   guessed part would return dates. Wrong dates are worse than
 *                   no dates: sensor.rtc() returning 0 says "no clock", and a
 *                   script can act on that, whereas a plausible timestamp from
 *                   the wrong chip is acted on as if it were true. board.h
 *                   records what is actually known about 0x51.
 *
 *   gpio.*          There is no driver for the expansion header, and the header
 *                   is the one place on this board where a wrong pin number
 *                   reaches something the firmware did not build. Wiring
 *                   gpio.write() to raw pinMode/digitalWrite would hand a
 *                   script the ability to drive the panel's chip-select or the
 *                   I2C bus by number, so this waits on a driver that knows
 *                   which pins are the header's.
 *
 *   service.http    another issue's; this one does not touch it. wifi is #82,
 *                   the radio.
 *
 *   fs.reset        sd_reset stays NULL, so fs.reset() reports "not available"
 *                   rather than doing nothing and saying it worked. Formatting
 *                   the card is #46. fs_base itself is wired - see below.
 */
#include <Arduino.h>

#include "display.h"
#include "net_time.h"
#include "ble.h"
#include "ble_hid.h"
#include "wifi.h"
#include "hal_meowkit.h"
#include "imu.h"
#include "input.h"
#include "input_names.h"
#include "led.h"
#include "pmu.h"
#include "rtc.h"
#include "sd_mount.h"

namespace {

/* Zero-initialised, so every hook not assigned in catnip_meowkit_hal_begin() is
 * NULL - which catnip_hal.h defines as "this call is a safe no-op". The ones
 * left out are listed at the top of this file with their reasons. */
catnip_hal g_hal;

void hal_led(void *ud, int r, int g, int b)
{
    (void)ud;
    /* Lua is not a typed language and device.led(300, -1, 0) is a thing a
     * script can write. Clamping is this boundary's job: the driver below takes
     * bytes and would otherwise be handed whatever the cast produced. */
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    /* The colour of the breath, not a steady level - see led.h for why an app
     * gets the hue and the firmware keeps the heartbeat. */
    catnip_led_colour((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

int hal_battery(void *ud)
{
    (void)ud;
    /* Already sampled by catnip_pmu_poll() from the main loop, and already -1
     * when there is nothing honest to say. Both halves of that matter: this
     * neither goes to the I2C bus on a script's schedule nor invents a number
     * when the answer is not known. */
    return catnip_pmu_battery_percent();
}

/* The clock, or 0 when this device does not know the time - which is what
 * rtc.h promises and what sensor.rtc() has always been specified to mean. This
 * hook was NULL until now, so the call already answered 0; what changes is that
 * the answer can become a time, and that when it is still 0 that is a measured
 * fact rather than an unwired pointer. */
int hal_wifi_status(void *ud)
{
    (void)ud;
    return catnip_wifi_status() == CATNIP_WIFI_CONNECTED ? 1 : 0;
}

const char *hal_wifi_ssid(void *ud)
{
    (void)ud;
    /* The SSID only while it is actually joined, so service.wifi.ssid() answers
     * NULL to an app whenever status() answers 0 - one truth, not two that can
     * disagree. */
    return catnip_wifi_status() == CATNIP_WIFI_CONNECTED ? catnip_wifi_ssid() : nullptr;
}

int hal_ble_scan(void *ud, catnip_ble_dev *out, int max)
{
    (void)ud;
    return catnip_ble_scan(out, max);
}

void hal_ble_rescan(void *ud)
{
    (void)ud;
    catnip_ble_rescan();
}

int hal_ble_mouse_begin(void *ud)
{
    (void)ud;
    return catnip_ble_hid_begin() ? 1 : 0;
}

void hal_ble_mouse_end(void *ud)
{
    (void)ud;
    catnip_ble_hid_end();
}

int hal_ble_mouse_state(void *ud)
{
    (void)ud;
    /* Connected is checked first because it implies up: asking the other way
     * round would answer 1 for a mouse that is actually in use. */
    if (catnip_ble_hid_connected()) return 2;
    return catnip_ble_hid_up() ? 1 : 0;
}

void hal_ble_mouse_move(void *ud, int dx, int dy, int buttons, int wheel)
{
    (void)ud;
    catnip_ble_hid_move(dx, dy, buttons, wheel);
}

void hal_wifi_rescan(void *ud)
{
    (void)ud;
    catnip_wifi_rescan();
}

int hal_wifi_scan(void *ud, catnip_wifi_ap *out, int max)
{
    (void)ud;
    return catnip_wifi_scan(out, max);
}

long hal_rtc_now(void *ud)
{
    (void)ud;
    return (long)catnip_rtc_now();
}

long hal_ntp_last(void *ud)
{
    (void)ud;
    return (long)catnip_net_time_last();
}

int hal_rtc_set(void *ud, long epoch)
{
    (void)ud;
    return catnip_rtc_set((uint32_t)epoch) ? 1 : 0;
}

void hal_brightness(void *ud, int pct)
{
    (void)ud;
    /* catnip_api.c has already clamped this to 0-100. Rounding to nearest
     * rather than truncating, so device.brightness(100) is 255 and not 254. */
    catnip_display_backlight((uint8_t)((pct * 255 + 50) / 100));
}

int hal_button(void *ud, const char *name)
{
    (void)ud;
    catnip_button button = catnip_button_from_name(name);

    if (button == CATNIP_BTN_COUNT) return 0; /* not a switch on this device */
    /* Whether it is held, not whether it was just pressed. The edge accessors
     * next to this one clear as they are read (see input.h), so calling one
     * here would mean whichever of the script and the firmware asked first
     * swallowed the press - and from Lua, that race would look like presses
     * going missing at random. Edges belong to whoever owns the input loop;
     * this is a level. */
    return catnip_input_down(button) ? 1 : 0;
}

void hal_imu(void *ud, float out[6])
{
    (void)ud;
    int32_t mg[3];
    int32_t mdps[3];

    /* Each half is written only if it was actually read, and an axis nothing
     * read stays the NaN it arrived as - reaching Lua as a missing field rather
     * than as a number. That is the whole point of the contract in
     * catnip_hal.h: writing 0.0 into an unread axis would be indistinguishable
     * from a device lying perfectly still, and an app reading m.gz to decide
     * whether it is being turned would believe it. A device is not at rest at
     * the origin because the bus was busy.
     *
     * The two halves are asked for separately even though one burst read
     * produces both (see imu.cpp), because "has an acceleration ever been read"
     * and "has a rate ever been read" are different claims and a caller of one
     * must not be answered on the strength of the other. */
    if (catnip_imu_acceleration(mg)) {
        /* Milli-g to g. The driver keeps integers because the one place it
         * prints them cannot rely on this build's printf having float support
         * (see imu.h); Lua numbers are doubles, so the conversion is here. */
        for (int i = 0; i < 3; i++)
            out[i] = (float)mg[i] / 1000.0f;
    }

    /* Milli-degrees per second to degrees per second, which is the unit
     * catnip_hal.h has always declared for these three - they were simply never
     * filled in, because the gyroscope was off until Air Mouse (#59) needed the
     * one question an accelerometer cannot answer. */
    if (catnip_imu_rotation(mdps)) {
        for (int i = 0; i < 3; i++)
            out[3 + i] = (float)mdps[i] / 1000.0f;
    }
}

} /* namespace */

const catnip_hal *catnip_meowkit_hal_begin(void)
{
    /* The LED, the PMIC and the panel are already up: main.cpp needs all three
     * before this point, to show a sign of life and a splash while the slower
     * parts of the boot run. Only the two nothing else has brought up are
     * started here. */
    catnip_input_begin();
    if (!catnip_imu_begin()) {
        /* Not fatal, and not silent. sensor.imu() will report no axes at all,
         * which is the truthful answer, but the reason belongs in the boot log
         * where it can be read next to what the I2C scan found. */
        Serial.println("[catnip] hal: no IMU, sensor.imu() will report nothing");
    }

    g_hal.led = hal_led;
    g_hal.battery = hal_battery;
    g_hal.brightness = hal_brightness;
    g_hal.button = hal_button;
    g_hal.imu = hal_imu;
    g_hal.rtc_now = hal_rtc_now;
    g_hal.rtc_set = hal_rtc_set;
    g_hal.ntp_last = hal_ntp_last;
    /* No longer the stubs this file used to apologise for: an app that asks
     * service.wifi.status() now gets the radio's real answer (#82). */
    g_hal.wifi_status = hal_wifi_status;
    g_hal.wifi_ssid = hal_wifi_ssid;
    g_hal.wifi_scan = hal_wifi_scan;
    g_hal.wifi_rescan = hal_wifi_rescan;
    g_hal.ble_scan = hal_ble_scan;
    g_hal.ble_rescan = hal_ble_rescan;
    g_hal.ble_mouse_begin = hal_ble_mouse_begin;
    g_hal.ble_mouse_end = hal_ble_mouse_end;
    g_hal.ble_mouse_state = hal_ble_mouse_state;
    g_hal.ble_mouse_move = hal_ble_mouse_move;

    /* fs.* is the card and nothing else. The root is the mount point itself,
     * because catnip_api.c reaches the card through plain stdio - fopen,
     * opendir, stat, remove on "<fs_base>/<name>" - rather than through the
     * SD_MMC object, and the mount point is the name the virtual filesystem
     * answers to. It carries no trailing separator for the same reason: the
     * join there is "%s/%s" and would otherwise produce "//".
     *
     * main.cpp mounts the card before this runs, and mounting is what settles
     * whether there is one, so this reads the answer rather than asking again.
     *
     * With no card the pointer stays NULL, which catnip_hal.h defines as "fs is
     * off" and catnip_api.c turns into an error an app can catch. That is the
     * point of leaving it NULL rather than naming /sd regardless: against a
     * mount point that is not there, fs.read() would return nil and
     * fs.exists() false - the same answers a working card gives for a file that
     * simply is not on it. No app could tell the two apart, and the File
     * Browser would cheerfully offer to write to a card nobody inserted. */
    if (catnip_sd_mounted()) {
        g_hal.fs_base = CATNIP_SD_MOUNT_POINT;
    } else {
        /* Not fatal, and not silent - the same treatment the IMU gets above.
         * The boot log already says "sd: no card"; this says what that costs. */
        Serial.println("[catnip] hal: no card, fs.* will report \"fs not available\"");
    }
    return &g_hal;
}

void catnip_meowkit_hal_set_fs(bool available)
{
    g_hal.fs_base = available ? CATNIP_SD_MOUNT_POINT : NULL;
}

void catnip_meowkit_hal_poll(void)
{
    /* The switches every pass, and only the switches. They are what a press
     * has to be seen by, and the loop's rate is the resolution of that.
     *
     * The other two are I2C reads on a 100 kHz bus, and each costs a few
     * hundred microseconds - at several hundred passes a second that was most
     * of the pass, spent asking two parts that cannot answer differently that
     * fast. The accelerometer is configured at 100 Hz and cannot report faster
     * than it samples; a battery moves over minutes. So each is asked at a rate
     * it can actually change at, which leaves the bus and the CPU for the
     * things that do. */
    static uint32_t imu_last;
    static uint32_t pmu_last;
    uint32_t now = millis();

    catnip_input_poll();
    if ((uint32_t)(now - imu_last) >= 20u) {
        imu_last = now;
        catnip_imu_poll();
    }
    if ((uint32_t)(now - pmu_last) >= 500u) {
        pmu_last = now;
        catnip_pmu_poll();
    }
}
