/* pmu.cpp - see pmu.h. */
#include <Arduino.h>

#include "board.h"
#include "i2cbus.h"
#include "pmu.h"

namespace {

/* AXP173 registers, per the datasheet. */
const uint8_t REG_RAIL_CONTROL  = 0x12; /* DC-DC1 / LDO4 / LDO2 / LDO3 enable */
const uint8_t REG_DCDC1_VOLTAGE = 0x26; /* 700-3500 mV in 25 mV steps */
const uint8_t REG_LDO4_VOLTAGE  = 0x27; /* 700-3500 mV in 25 mV steps */
const uint8_t REG_LDO23_VOLTAGE = 0x28; /* LDO2 high nibble, LDO3 low nibble; 1800-3300 mV in 100 mV steps */

/* Bits within REG_RAIL_CONTROL. Bit 0 is DC-DC1, which supplies the MCU: this
 * code must never clear it, so the enable write is read-modify-OR. */
const uint8_t BIT_DCDC1 = 1u << 0;
const uint8_t BIT_LDO4  = 1u << 1;
const uint8_t BIT_LDO2  = 1u << 2;
const uint8_t BIT_LDO3  = 1u << 3;
const uint8_t ALL_RAILS = BIT_DCDC1 | BIT_LDO4 | BIT_LDO2 | BIT_LDO3; /* stock expects 0x0F */

/* Every rail on this board is 3.3 V; the stock firmware sets all four before
 * it touches anything else. Relying on what the PMIC remembers from the last
 * firmware works until the battery has been out. */
const uint16_t RAIL_MILLIVOLTS = 3300;

bool read_reg(uint8_t reg, uint8_t *out)
{
    return catnip_i2c_read_reg(CATNIP_I2C_ADDR_PMU, reg, out);
}

bool write_reg(uint8_t reg, uint8_t value)
{
    return catnip_i2c_write_reg(CATNIP_I2C_ADDR_PMU, reg, value);
}

uint8_t encode_25mv(uint16_t mv)
{
    /* The step is 25 mV from a 700 mV floor. */
    return (uint8_t)((mv - 700) / 25);
}

uint8_t encode_100mv_nibble(uint16_t mv)
{
    /* The step is 100 mV from an 1800 mV floor. */
    return (uint8_t)((mv - 1800) / 100);
}

} /* namespace */

bool catnip_pmu_begin(void)
{
    uint8_t rails = 0;
    if (!read_reg(REG_RAIL_CONTROL, &rails)) {
        Serial.println("[catnip] pmu: no answer");
        return false;
    }

    /* Set the voltages before enabling, so a rail never comes up at whatever
     * the previous setting happened to be. Writing unconditionally is cheaper
     * than reading first, and the values are constants. */
    const uint8_t v25    = encode_25mv(RAIL_MILLIVOLTS);
    const uint8_t nibble = encode_100mv_nibble(RAIL_MILLIVOLTS);
    const uint8_t v23    = (uint8_t)((nibble << 4) | nibble);
    if (!write_reg(REG_DCDC1_VOLTAGE, v25) ||
        !write_reg(REG_LDO4_VOLTAGE, v25) ||
        !write_reg(REG_LDO23_VOLTAGE, v23)) {
        Serial.println("[catnip] pmu: voltage write failed");
        return false;
    }

    /* OR only. Clearing bit 0 here would cut power to the MCU running this. */
    const bool needs_enable = (rails & ALL_RAILS) != ALL_RAILS;
    if (needs_enable && !write_reg(REG_RAIL_CONTROL, (uint8_t)(rails | ALL_RAILS))) {
        Serial.println("[catnip] pmu: rail enable failed");
        return false;
    }

    Serial.printf("[catnip] pmu: rails were 0x%02X, all at 3.3V%s\n",
                  rails, needs_enable ? ", enabled the missing ones" : "");

    /* A rail that just came up needs a moment before what hangs off it will
     * answer. */
    if (needs_enable) delay(50);
    return true;
}
