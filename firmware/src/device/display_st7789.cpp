/*
 * display_st7789.cpp - ST7789 bring-up over SPI (issue #29).
 *
 * The panel configuration below is a property of the MeowKit's wiring, not a
 * preference: the part is a 240x320 die mounted sideways, its chip-select and
 * reset live on the I/O expander (see ioexp.cpp) rather than on MCU pins, MISO
 * is not wired, and it inverts. Getting any of those wrong
 * gives a blank, mirrored, or colour-swapped screen, so they live in board.h
 * next to the pin numbers rather than being scattered here.
 */
#include <LovyanGFX.hpp>

#include "board.h"
#include "display.h"

namespace {

class Panel : public lgfx::LGFX_Device {
public:
    Panel()
    {
        {
            auto cfg = bus_.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = CATNIP_LCD_SPI_HZ;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;  /* the panel has a real D/C pin; 9-bit mode would garble it */
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = CATNIP_PIN_LCD_SCLK;
            cfg.pin_mosi = CATNIP_PIN_LCD_MOSI;
            cfg.pin_miso = CATNIP_PIN_LCD_MISO;
            cfg.pin_dc   = CATNIP_PIN_LCD_DC;
            bus_.config(cfg);
            panel_.setBus(&bus_);
        }
        {
            auto cfg = panel_.config();
            cfg.pin_cs   = CATNIP_PIN_LCD_CS;
            cfg.pin_rst  = CATNIP_PIN_LCD_RST;
            cfg.pin_busy = -1;
            cfg.panel_width  = CATNIP_LCD_PANEL_W;
            cfg.panel_height = CATNIP_LCD_PANEL_H;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            /* The die is mounted sideways; this presents it as 320x240. */
            cfg.offset_rotation = CATNIP_LCD_ROTATION;
            cfg.readable   = false;   /* write-only: MISO is not connected */
            cfg.invert     = CATNIP_LCD_INVERT;
            cfg.rgb_order  = CATNIP_LCD_RGB_ORDER;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            panel_.config(cfg);
        }
        {
            auto cfg = light_.config();
            cfg.pin_bl = CATNIP_PIN_LCD_BL;
            cfg.invert = false;
            cfg.freq   = CATNIP_LCD_BL_PWM_HZ;
            cfg.pwm_channel = 7;
            light_.config(cfg);
            panel_.setLight(&light_);
        }
        setPanel(&panel_);
    }

private:
    lgfx::Panel_ST7789 panel_;
    lgfx::Bus_SPI      bus_;
    lgfx::Light_PWM    light_;
};

Panel g_panel;
bool  g_up = false;

} /* namespace */

bool catnip_display_begin(void)
{
    if (g_up) return true;
    if (!g_panel.init()) return false;
    /* Backlight stays off until someone draws, so boot does not flash noise. */
    g_panel.setBrightness(0);
    g_panel.fillScreen(0);
    g_up = true;
    return true;
}

void catnip_display_backlight(uint8_t level)
{
    if (g_up) g_panel.setBrightness(level);
}

void catnip_display_blit(const void *data)
{
    if (!g_up || !data) return;
    g_panel.startWrite();
    g_panel.pushImage(0, 0, CATNIP_SCREEN_W, CATNIP_SCREEN_H,
                      static_cast<const lgfx::rgb565_t *>(data));
    g_panel.endWrite();
}

void catnip_display_fill(uint16_t rgb565)
{
    if (g_up) g_panel.fillScreen(rgb565);
}
