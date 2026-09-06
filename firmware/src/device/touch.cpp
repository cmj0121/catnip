/* touch.cpp - see touch.h. */
#include <Arduino.h>

#include "board.h"
#include "i2cbus.h"
#include "touch.h"
#include "touch_map.h"

/* The FT6336's own register map. board.h owns the bus address, because that is
 * a fact about this board's wiring; these are facts about the part, and belong
 * with the only code that talks to it.
 *
 * TD_STATUS counts the fingers currently down in its low nibble. The first
 * point follows in the four registers from 0x03: the high byte and then the
 * low byte of X, then the same for Y. Only the low nibble of each high byte is
 * coordinate - the bits above it carry an event flag on X and a touch id on Y
 * - so they are masked off. The probe read them this way on the device and got
 * positions that fall inside the panel. */
static const uint8_t kRegTdStatus = 0x02;
static const uint8_t kRegP1Xh = 0x03;
static const uint8_t kRegChipId = 0xA3;
static const uint8_t kRegVendorId = 0xA8;

/* What an FT6336 answers with. Measured on this device: 0xA3 read 0x64 and
 * 0xA8 read 0x11. */
static const uint8_t kChipIdFt6336 = 0x64;
static const uint8_t kVendorIdFt6336 = 0x11;

static bool g_present;
static bool g_down;
static bool g_tapped;
static bool g_lifted;
static bool g_have_position;
static uint16_t g_x;
static uint16_t g_y;
static bool g_have_panel;
static uint16_t g_panel_x;
static uint16_t g_panel_y;

/* Read the first touch point and turn it into a screen position. Four separate
 * register reads are not one atomic sample - a finger moving between them
 * could pair an old X with a new Y - and that is tolerable for the same reason
 * it was in the probe: the error is at most one poll's worth of movement, on a
 * position that is only ever used to say where on the screen the finger is. */
static bool read_position(uint16_t *screen_x, uint16_t *screen_y)
{
    uint8_t r[4];

    for (uint8_t i = 0; i < 4; i++) {
        if (!catnip_i2c_read_reg(CATNIP_I2C_ADDR_TOUCH, (uint8_t)(kRegP1Xh + i), &r[i]))
            return false;
    }

    uint16_t panel_x = (uint16_t)(((r[0] & 0x0F) << 8) | r[1]);
    uint16_t panel_y = (uint16_t)(((r[2] & 0x0F) << 8) | r[3]);

    /* Keep the raw pair whether or not the rotation accepts it. A coordinate
     * that decodes to a point off the panel is precisely what the diagnostic
     * page has to be able to show: dropping it here would leave the page
     * displaying a stale mapped position with nothing on screen to say why it
     * had stopped moving. */
    g_panel_x = panel_x;
    g_panel_y = panel_y;
    g_have_panel = true;

    return catnip_touch_panel_to_screen(panel_x, panel_y, screen_x, screen_y);
}

bool catnip_touch_begin(void)
{
    uint8_t chip = 0;
    uint8_t vendor = 0;

    g_present = false;
    g_down = false;
    g_tapped = false;
    g_lifted = false;
    g_have_position = false;
    g_have_panel = false;

    if (!catnip_i2c_read_reg(CATNIP_I2C_ADDR_TOUCH, kRegChipId, &chip) ||
        !catnip_i2c_read_reg(CATNIP_I2C_ADDR_TOUCH, kRegVendorId, &vendor)) {
        /* Either nothing is at the address or it did not answer a register
         * read. The bus scan in catnip_i2c_scan() is what separates the two,
         * and it is worth running when this line appears. */
        Serial.printf("[catnip] touch: 0x%02X did not answer its identity registers\n",
                      CATNIP_I2C_ADDR_TOUCH);
        return false;
    }

    if (chip != kChipIdFt6336 || vendor != kVendorIdFt6336) {
        /* Say what answered rather than carrying on. Interpreting some other
         * part's registers as an FT6336's would produce finger positions that
         * look like a miscalibrated panel, and days would go into the
         * calibration before anyone suspected the chip. */
        Serial.printf("[catnip] touch: 0x%02X reports chip 0x%02X vendor 0x%02X, not the "
                      "FT6336's 0x%02X/0x%02X - not reading it as one\n",
                      CATNIP_I2C_ADDR_TOUCH, chip, vendor, kChipIdFt6336,
                      kVendorIdFt6336);
        return false;
    }

    g_present = true;
    return true;
}

void catnip_touch_poll(void)
{
    uint8_t td;

    if (!g_present) return;

    if (!catnip_i2c_read_reg(CATNIP_I2C_ADDR_TOUCH, kRegTdStatus, &td)) return;

    bool down = (td & 0x0F) != 0;

    if (down) {
        uint16_t x;
        uint16_t y;

        /* A position that does not read, or that decodes to a point off the
         * panel, leaves the last one standing. The finger is still down either
         * way, and a stale position is closer to the truth than a jump to
         * wherever the noise decoded to. */
        if (read_position(&x, &y)) {
            g_x = x;
            g_y = y;
            g_have_position = true;
        }
    }

    if (down != g_down) {
        g_down = down;
        if (down) {
            g_tapped = true;
        } else {
            g_lifted = true;
        }
    }
}

bool catnip_touch_down(void)
{
    return g_down;
}

bool catnip_touch_tapped(void)
{
    bool seen = g_tapped;

    g_tapped = false;
    return seen;
}

bool catnip_touch_lifted(void)
{
    bool seen = g_lifted;

    g_lifted = false;
    return seen;
}

bool catnip_touch_position(uint16_t *screen_x, uint16_t *screen_y)
{
    if (!g_have_position) return false;

    *screen_x = g_x;
    *screen_y = g_y;
    return true;
}

bool catnip_touch_panel_position(uint16_t *panel_x, uint16_t *panel_y)
{
    if (!g_have_panel) return false;

    *panel_x = g_panel_x;
    *panel_y = g_panel_y;
    return true;
}
