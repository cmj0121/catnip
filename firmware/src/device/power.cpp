/* power.cpp - see power.h. */
#include <Arduino.h>

#include "board.h"
#include "power.h"

void catnip_power_hold(void)
{
    pinMode(CATNIP_PIN_PWR_HOLD, OUTPUT);
    digitalWrite(CATNIP_PIN_PWR_HOLD, HIGH);
    pinMode(CATNIP_PIN_PWR_ON, INPUT);
}

bool catnip_power_button_down(void)
{
    return digitalRead(CATNIP_PIN_PWR_ON) == HIGH;
}
