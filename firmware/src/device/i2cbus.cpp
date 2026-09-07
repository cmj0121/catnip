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

bool catnip_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool catnip_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *out)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, 1) != 1) return false;
    *out = (uint8_t)Wire.read();
    return true;
}

bool catnip_i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *out, size_t len)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, (int)len) != (int)len) return false;
    for (size_t i = 0; i < len; i++) {
        out[i] = (uint8_t)Wire.read();
    }
    return true;
}

bool catnip_i2c_write_regs(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    /* Wire.write() returns short when its buffer fills. Checking it is what
     * turns "the caller asked for more than this bus can carry in one go" into
     * a refusal, rather than a partial write the device would acknowledge
     * quite happily and the caller would read as success. */
    if (Wire.write(data, len) != len) {
        Wire.endTransmission();
        return false;
    }
    return Wire.endTransmission() == 0;
}
