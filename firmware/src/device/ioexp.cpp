/*
 * ioexp.cpp - see ioexp.h. The pin map and why it differs from the vendor's
 * published source are documented in board.h.
 *
 * Registers are written as absolute values, never read-modify-write: the
 * expander sits on an always-on rail, so its registers survive an ESP32 reset
 * and a read-modify-write inherits whatever the last firmware left behind.
 */
#include <Arduino.h>

#include "board.h"
#include "i2cbus.h"
#include "ioexp.h"

namespace {

/* PCA9557 register map. Configuration bits are 1 for input, 0 for output. */
const uint8_t REG_OUTPUT = 0x01;
const uint8_t REG_CONFIG = 0x03;

const uint8_t BIT_LCD_RST = (uint8_t)(1u << CATNIP_IOEXP_LCD_RST);
const uint8_t BIT_LCD_CS  = (uint8_t)(1u << CATNIP_IOEXP_LCD_CS);
const uint8_t BIT_IO3     = (uint8_t)(1u << CATNIP_IOEXP_IO3);
const uint8_t BIT_AUX_RST = (uint8_t)(1u << CATNIP_IOEXP_AUX_RST);

/* The four pins we drive; everything else stays an input. */
const uint8_t OUTPUT_PINS = (uint8_t)(BIT_LCD_RST | BIT_LCD_CS | BIT_IO3 | BIT_AUX_RST);

bool set_outputs(uint8_t levels)
{
    return catnip_i2c_write_reg(CATNIP_I2C_ADDR_IOEXP, REG_OUTPUT, levels);
}

} /* namespace */

bool catnip_ioexp_begin(void)
{
    /* Step 1: CS deasserted, both resets and IO3 asserted low. Retry, because
     * a single NAK on a freshly powered bus is not proof of absence. */
    bool acked = false;
    for (int attempt = 0; attempt < 5 && !acked; attempt++) {
        acked = set_outputs(BIT_LCD_CS) &&
                catnip_i2c_write_reg(CATNIP_I2C_ADDR_IOEXP, REG_CONFIG, (uint8_t)~OUTPUT_PINS);
        if (!acked) delay(20);
    }
    if (!acked) return false;
    delay(CATNIP_IOEXP_RST_LOW_MS);

    /* Step 2: release the resets; the panel needs time before its first
     * command, which is why CS stays high for now. */
    if (!set_outputs(BIT_LCD_CS | BIT_LCD_RST | BIT_AUX_RST)) return false;
    delay(CATNIP_IOEXP_RST_HIGH_MS);

    /* Step 3: select the panel and leave it selected. */
    if (!set_outputs(BIT_LCD_RST | BIT_AUX_RST)) return false;
    delay(CATNIP_IOEXP_CS_SETTLE_MS);

    /* Read back rather than echo the constants: this line is the evidence that
     * the values reached the chip, which is what a dark screen makes you doubt. */
    uint8_t cfg = 0, out = 0;
    if (catnip_i2c_read_reg(CATNIP_I2C_ADDR_IOEXP, REG_CONFIG, &cfg) &&
        catnip_i2c_read_reg(CATNIP_I2C_ADDR_IOEXP, REG_OUTPUT, &out)) {
        Serial.printf("[catnip] ioexp: cfg=0x%02X out=0x%02X\n", cfg, out);
    } else {
        Serial.println("[catnip] ioexp: written, but read-back failed");
    }
    return true;
}
