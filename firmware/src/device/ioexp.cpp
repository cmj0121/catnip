/* ioexp.cpp - see ioexp.h. */
#include <Arduino.h>
#include <Wire.h>

#include "board.h"
#include "ioexp.h"

namespace {

/* PCA9557 register map. Configuration bits are 1 for input, 0 for output. */
const uint8_t REG_OUTPUT = 0x01;
const uint8_t REG_CONFIG = 0x03;

bool write_reg(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(CATNIP_I2C_ADDR_IOEXP);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

} /* namespace */

bool catnip_ioexp_begin(void)
{
    /* Match the vendor's configuration exactly, because that one demonstrably
     * drives this panel. IO0 is the chip-select and goes low; IO1 is the
     * speaker amplifier's enable and goes high. Everything else stays an
     * input.
     *
     * IO1 was previously left as an input on the reasoning that claiming it
     * would pop the speaker on every boot. That reasoning cost more than it
     * saved: this is the one configuration known to work, and deviating from
     * it while the display was still dark meant never knowing which deviation
     * mattered. Retry, because a single NAK on a freshly powered bus is not
     * proof of absence. */
    const uint8_t outputs = (uint8_t)(1u << CATNIP_IOEXP_PA_EN);          /* CS low, PA_EN high */
    const uint8_t config  = (uint8_t)~((1u << CATNIP_IOEXP_LCD_CS) |
                                       (1u << CATNIP_IOEXP_PA_EN));       /* those two are outputs */
    for (int attempt = 0; attempt < 5; attempt++) {
        if (write_reg(REG_OUTPUT, outputs) && write_reg(REG_CONFIG, config)) {
            return true;
        }
        delay(20);
    }
    return false;
}
