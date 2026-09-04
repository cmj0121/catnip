/* i2cbus.cpp - see i2cbus.h. */
#include <Arduino.h>
#include <Wire.h>

#include "board.h"
#include "i2cbus.h"

void catnip_i2c_begin(void)
{
    Wire.begin(CATNIP_PIN_I2C_SDA, CATNIP_PIN_I2C_SCL);
    Wire.setClock(CATNIP_I2C_HZ);
    delay(200);
}

void catnip_i2c_scan(void)
{
    Serial.print("[catnip] i2c:");
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf(" 0x%02X", addr);
            found++;
        }
    }
    if (!found) Serial.print(" (nothing responded)");
    Serial.println();
}
