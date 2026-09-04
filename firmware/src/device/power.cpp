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
