/* pmu.cpp - see pmu.h. */
#include <Arduino.h>

#include "battery_gauge.h"
#include "board.h"
#include "i2cbus.h"
#include "pmu.h"

namespace {

/* AXP173 registers, per the datasheet. */
const uint8_t REG_RAIL_CONTROL = 0x12;  /* DC-DC1 / LDO4 / LDO2 / LDO3 enable */
const uint8_t REG_DCDC1_VOLTAGE = 0x26; /* 700-3500 mV in 25 mV steps */
const uint8_t REG_LDO4_VOLTAGE = 0x27;  /* 700-3500 mV in 25 mV steps */
const uint8_t REG_LDO23_VOLTAGE =
    0x28; /* LDO2 high nibble, LDO3 low; 100 mV steps from 1800 */
/* The interrupt-enable register is this driver's own business. Its status
 * counterpart and the two power-key bits are in pmu.h instead, because the
 * probe reads the same register while it is sweeping every other input, and
 * one part deserves one definition. */
const uint8_t REG_IRQ_ENABLE_3 = 0x42; /* bit 1 short press, bit 0 long press */

/* The battery side, per the datasheet - and, unlike the rails above, not yet
 * confirmed against this board. See catnip_pmu_battery_percent() in pmu.h for
 * what that means for the number it reports. */
const uint8_t REG_POWER_MODE = 0x01; /* bit 5 set while a battery is attached */
const uint8_t BIT_BATTERY_PRESENT = 1u << 5;
const uint8_t REG_ADC_ENABLE_1 = 0x82; /* bit 7 enables the battery-voltage ADC */
const uint8_t BIT_ADC_BATTERY_VOLTAGE = 1u << 7;
/* 12 bits across two registers: 0x78 holds the high 8, 0x79 the low 4. */
const uint8_t REG_BATTERY_VOLTAGE = 0x78;

/* The ADC step is 1.1 mV. Kept as a numerator over a denominator rather than a
 * float so the conversion is exact integer arithmetic, the same reason the IMU
 * reports milli-g. */
const uint32_t VOLTAGE_STEP_NUM = 11;
const uint32_t VOLTAGE_STEP_DEN = 10;

/* Five seconds - see catnip_pmu_poll() in pmu.h for why it is this long. */
const uint32_t kPollIntervalMs = 5000;

/* Whether the PMIC answered at boot. Without it, a board with no PMIC would
 * have every poll go to the bus and time out. */
bool g_present = false;

/* -1 until a plausible reading has been taken, and back to -1 whenever one
 * stops being available. Nothing here ever holds a stale percentage: a battery
 * that has been unplugged is not still at 60%. */
int g_battery_percent = -1;
uint32_t g_last_poll_ms = 0;

/* Bits within REG_RAIL_CONTROL. Bit 0 is DC-DC1, which supplies the MCU: this
 * code must never clear it, so the enable write is read-modify-OR. */
const uint8_t BIT_DCDC1 = 1u << 0;
const uint8_t BIT_LDO4 = 1u << 1;
const uint8_t BIT_LDO2 = 1u << 2;
const uint8_t BIT_LDO3 = 1u << 3;
const uint8_t ALL_RAILS =
    BIT_DCDC1 | BIT_LDO4 | BIT_LDO2 | BIT_LDO3; /* stock expects 0x0F */

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
    const uint8_t v25 = encode_25mv(RAIL_MILLIVOLTS);
    const uint8_t nibble = encode_100mv_nibble(RAIL_MILLIVOLTS);
    const uint8_t v23 = (uint8_t)((nibble << 4) | nibble);
    if (!write_reg(REG_DCDC1_VOLTAGE, v25) || !write_reg(REG_LDO4_VOLTAGE, v25) ||
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

    Serial.printf("[catnip] pmu: rails were 0x%02X, all at 3.3V%s\n", rails,
                  needs_enable ? ", enabled the missing ones" : "");

    /* Latch power-button presses. The button is not wired to any MCU pin on
     * this board, so this is the only way the firmware can see it. */
    uint8_t irq_enable = 0;
    if (read_reg(REG_IRQ_ENABLE_3, &irq_enable)) {
        write_reg(REG_IRQ_ENABLE_3, (uint8_t)(irq_enable | CATNIP_PMU_IRQ_PEK_SHORT |
                                              CATNIP_PMU_IRQ_PEK_LONG));
    }
    /* Clear whatever is pending, including the press that switched the device
     * on - otherwise the first thing the firmware does is act on it. */
    uint8_t pending = 0;
    if (read_reg(CATNIP_PMU_REG_IRQ_STATUS_3, &pending) && pending) {
        write_reg(CATNIP_PMU_REG_IRQ_STATUS_3, pending);
    }

    /* Ask the PMIC to measure the battery. Read-modify-OR rather than a plain
     * write, for the same reason the rail control register is: the other bits
     * in here enable ADCs this driver does not read but has no business
     * switching off. */
    uint8_t adc = 0;
    if (read_reg(REG_ADC_ENABLE_1, &adc)) {
        write_reg(REG_ADC_ENABLE_1, (uint8_t)(adc | BIT_ADC_BATTERY_VOLTAGE));
    }

    /* A rail that just came up needs a moment before what hangs off it will
     * answer. */
    if (needs_enable) delay(50);
    g_present = true;
    return true;
}

void catnip_pmu_poll(void)
{
    uint8_t mode = 0;
    uint8_t raw[2];
    uint32_t counts, mv;

    if (!g_present) return;

    /* Unsigned subtraction, so this stays correct across the wrap of millis()
     * every 49 days - the same arithmetic the IMU's budget and the bounce
     * filter's clock use. */
    uint32_t now = millis();
    /* The zero check is so the first call always reads: at boot g_last_poll_ms
     * is 0 and millis() is already a few thousand, which for a budget this long
     * would otherwise skip the first poll and leave the battery unknown for
     * five seconds after the screen is up. */
    if (g_last_poll_ms != 0 && now - g_last_poll_ms < kPollIntervalMs) return;
    g_last_poll_ms = now;

    if (!read_reg(REG_POWER_MODE, &mode) || !(mode & BIT_BATTERY_PRESENT)) {
        /* No battery attached: the device is running off USB. That is not a
         * failure and it is not zero percent. */
        g_battery_percent = -1;
        return;
    }

    /* Both halves in one transaction, so the low nibble belongs to the same
     * conversion as the high byte. */
    if (!catnip_i2c_read_regs(CATNIP_I2C_ADDR_PMU, REG_BATTERY_VOLTAGE, raw,
                              sizeof(raw))) {
        g_battery_percent = -1;
        return;
    }

    counts = ((uint32_t)raw[0] << 4) | (uint32_t)(raw[1] & 0x0F);
    mv = counts * VOLTAGE_STEP_NUM / VOLTAGE_STEP_DEN;

    /* The estimate, and the refusal to make one, are both in battery_gauge.h,
     * where a host test can reach them - decoding the registers is this
     * driver's job, deciding what the number means is not. */
    g_battery_percent = catnip_battery_percent_from_mv(mv);
    if (g_battery_percent < 0) {
        /* Say so rather than failing silently: a register map that turned out
         * to be wrong is exactly what someone needs to be told about, and this
         * log line is the only place it would surface. */
        Serial.printf("[catnip] pmu: battery ADC read %lu mV, which is not a cell\n",
                      (unsigned long)mv);
    }
}

int catnip_pmu_battery_percent(void)
{
    return g_battery_percent;
}

/* Both key events come from one register, and reading it clears every bit
 * that was set - so read once and answer for the bit that was asked about,
 * remembering the other for its own caller. Without that, whichever of the
 * two ran first would swallow the other's press. */
static bool take_key_event(uint8_t bit)
{
    static uint8_t pending = 0;

    uint8_t status = 0;
    if (read_reg(CATNIP_PMU_REG_IRQ_STATUS_3, &status) &&
        (status & (CATNIP_PMU_IRQ_PEK_SHORT | CATNIP_PMU_IRQ_PEK_LONG))) {
        write_reg(CATNIP_PMU_REG_IRQ_STATUS_3,
                  status); /* write 1 to clear what was set */
        pending |=
            (uint8_t)(status & (CATNIP_PMU_IRQ_PEK_SHORT | CATNIP_PMU_IRQ_PEK_LONG));
    }
    if (!(pending & bit)) return false;
    pending &= (uint8_t)~bit;
    return true;
}

bool catnip_pmu_power_key_pressed(void)
{
    return take_key_event(CATNIP_PMU_IRQ_PEK_SHORT);
}

bool catnip_pmu_power_key_held(void)
{
    return take_key_event(CATNIP_PMU_IRQ_PEK_LONG);
}
