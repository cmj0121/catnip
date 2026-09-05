/* power.cpp - see power.h. */
#include <Arduino.h>

#include "board.h"
#include "power.h"

void catnip_power_hold(void)
{
    pinMode(CATNIP_PIN_PWR_HOLD, OUTPUT);
    digitalWrite(CATNIP_PIN_PWR_HOLD, HIGH);
    /* GPIO10 is the vendor's PWR_ON. Nothing reads it - the button reaches the
     * PMIC, not the MCU (see board.h) - but leave it pulled up rather than
     * floating. */
    pinMode(CATNIP_PIN_PWR_ON, INPUT_PULLUP);
}

void catnip_power_off(void)
{
    /* Dropping PWR_HOLD cuts the rail the board is running from - but only when
     * the board is the thing supplying it. With USB attached the chip carries on
     * regardless, and returning from here would drop the caller back into its
     * loop, which lights the status LED again on its very next pass: a device
     * that says it switched off, shows nothing, and still breathes at you.
     *
     * So do not return. On battery the rail is gone before this loop gets far;
     * on USB the device sits dark and idle, which is what "off" should look
     * like. delay() rather than a bare spin, so the idle tasks still run and
     * the watchdog stays quiet. */
    digitalWrite(CATNIP_PIN_PWR_HOLD, LOW);
    for (;;) {
        delay(100);
    }
}
