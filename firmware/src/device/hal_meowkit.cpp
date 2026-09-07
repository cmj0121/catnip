/*
 * hal_meowkit.cpp - see hal_meowkit.h. Issue #35.
 *
 * This file is a wiring diagram and nothing else. Every hook below is a few
 * lines that turn a Lua-shaped request into a call on a driver in this
 * directory; anything that needed thinking about belongs in the driver, and if
 * a hook here starts growing logic that is the sign it went to the wrong place.
 *
 * What is live: the status LED, the battery, the backlight, the seven switches
 * and the accelerometer.
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
 *   service.*       wifi and http are another issue's; this one does not touch
 *                   the radio.
 *
 *   fs.*            fs_base and sd_reset stay NULL here. The card is mounted in
 *                   main.cpp and giving apps a filesystem is #32's scope, not
 *                   this file's.
 */
#include <Arduino.h>

#include "display.h"
#include "hal_meowkit.h"
#include "imu.h"
#include "input.h"
#include "input_names.h"
#include "led.h"
#include "pmu.h"

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

    /* The three gyroscope axes are left exactly as they arrived, which is NaN,
     * and reach Lua as missing fields rather than as numbers. This is the whole
     * point of the contract in catnip_hal.h: the BMI270 on this board has its
     * gyroscope deliberately switched off (imu.h explains why - nothing here
     * needs a rate, and running it would cost current and bus time for no
     * caller), so there is no rate to report. Writing 0.0 into them would be
     * indistinguishable from a device lying perfectly still, and an app reading
     * m.gz to decide whether it is being turned would believe it.
     *
     * The accelerometer's axes are left NaN too when nothing has been read yet
     * or the part is absent, for the same reason: a device is not at rest at
     * the origin because the bus was busy. */
    if (!catnip_imu_acceleration(mg)) return;

    /* Milli-g to g. The driver keeps integers because the one place it prints
     * them cannot rely on this build's printf having float support (see
     * imu.h); Lua numbers are doubles, so the conversion happens here. */
    for (int i = 0; i < 3; i++)
        out[i] = (float)mg[i] / 1000.0f;
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
    return &g_hal;
}

void catnip_meowkit_hal_poll(void)
{
    catnip_input_poll();
    catnip_imu_poll();
    catnip_pmu_poll();
}
