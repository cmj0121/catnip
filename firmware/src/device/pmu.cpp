/* pmu.cpp - see pmu.h. */
#include <Arduino.h>
#include <Wire.h>

#include "board.h"
#include "pmu.h"

namespace {

/* AXP173 registers, per the datasheet. */
const uint8_t REG_RAIL_CONTROL = 0x12; /* DC-DC1 / LDO4 / LDO2 / LDO3 enable */
const uint8_t REG_LDO4_VOLTAGE = 0x27; /* 700-3500 mV in 25 mV steps */

/* Bits within REG_RAIL_CONTROL. Bit 0 is DC-DC1, which supplies the MCU: this
 * code must never clear it, so every write here is read-modify-OR. */
const uint8_t BIT_DCDC1 = 1u << 0;
const uint8_t BIT_LDO4  = 1u << 1;
const uint8_t BIT_LDO2  = 1u << 2;
const uint8_t BIT_LDO3  = 1u << 3;

/* The expander's rail is labelled LDO4_3V3 on the schematic. */
const uint16_t LDO4_MILLIVOLTS = 3300;

bool read_reg(uint8_t reg, uint8_t *out)
{
    Wire.beginTransmission(CATNIP_I2C_ADDR_PMU);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)CATNIP_I2C_ADDR_PMU, 1) != 1) return false;
    *out = (uint8_t)Wire.read();
    return true;
}

bool write_reg(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(CATNIP_I2C_ADDR_PMU);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

uint8_t encode_millivolts(uint16_t mv)
{
    /* The step is 25 mV from a 700 mV floor. */
    return (uint8_t)((mv - 700) / 25);
}

} /* namespace */

bool catnip_pmu_begin(void)
{
    uint8_t rails = 0, ldo4_mv = 0;
    if (!read_reg(REG_RAIL_CONTROL, &rails)) {
        Serial.println("[catnip] pmu: no answer");
        return false;
    }
    read_reg(REG_LDO4_VOLTAGE, &ldo4_mv);
    Serial.printf("[catnip] pmu: rails=0x%02X (dcdc1=%d ldo2=%d ldo3=%d ldo4=%d) ldo4_reg=0x%02X\n",
                  rails, !!(rails & BIT_DCDC1), !!(rails & BIT_LDO2),
                  !!(rails & BIT_LDO3), !!(rails & BIT_LDO4), ldo4_mv);

    /* Set the voltage before enabling, so the rail never comes up at whatever
     * the previous setting happened to be. */
    const uint8_t want = encode_millivolts(LDO4_MILLIVOLTS);
    if (ldo4_mv != want && !write_reg(REG_LDO4_VOLTAGE, want)) {
        Serial.println("[catnip] pmu: LDO4 voltage write failed");
        return false;
    }

    /* OR only. Clearing bit 0 here would cut power to the MCU running this. */
    if (!(rails & BIT_LDO4)) {
        if (!write_reg(REG_RAIL_CONTROL, (uint8_t)(rails | BIT_LDO4))) {
            Serial.println("[catnip] pmu: LDO4 enable failed");
            return false;
        }
        Serial.println("[catnip] pmu: LDO4 enabled");
        /* The expander needs a moment before it will answer. */
        delay(50);
    } else {
        Serial.println("[catnip] pmu: LDO4 already on");
    }
    return true;
}
