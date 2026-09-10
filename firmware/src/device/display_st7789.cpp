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
/* SD_MMC comes first on purpose: LovyanGFX only compiles its file-reading
 * helpers when it can see a filesystem class, and drawJpgFile below is one of
 * them. */
#include <SD_MMC.h>

#include <LovyanGFX.hpp>

#include <string.h>

#include "board.h"
#include "display.h"

#include "catnip_config.h" /* for the path length the config file allows */

namespace {

class Panel : public lgfx::LGFX_Device {
  public:
    Panel()
    {
        {
            auto cfg = bus_.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = CATNIP_LCD_SPI_HZ;
            cfg.freq_read = 16000000;
            cfg.spi_3wire =
                false; /* the panel has a real D/C pin; 9-bit mode would garble it */
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = CATNIP_PIN_LCD_SCLK;
            cfg.pin_mosi = CATNIP_PIN_LCD_MOSI;
            cfg.pin_miso = CATNIP_PIN_LCD_MISO;
            cfg.pin_dc = CATNIP_PIN_LCD_DC;
            bus_.config(cfg);
            panel_.setBus(&bus_);
        }
        {
            auto cfg = panel_.config();
            cfg.pin_cs = CATNIP_PIN_LCD_CS;
            cfg.pin_rst = CATNIP_PIN_LCD_RST;
            cfg.pin_busy = -1;
            cfg.panel_width = CATNIP_LCD_PANEL_W;
            cfg.panel_height = CATNIP_LCD_PANEL_H;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            /* The die is mounted sideways; this presents it as 320x240. */
            cfg.offset_rotation = CATNIP_LCD_ROTATION;
            cfg.readable = false; /* write-only: MISO is not connected */
            cfg.invert = CATNIP_LCD_INVERT;
            cfg.rgb_order = CATNIP_LCD_RGB_ORDER;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            panel_.config(cfg);
        }
        {
            auto cfg = light_.config();
            cfg.pin_bl = CATNIP_PIN_LCD_BL;
            cfg.invert = CATNIP_LCD_BL_INVERT;
            cfg.freq = CATNIP_LCD_BL_PWM_HZ;
            cfg.pwm_channel = 7;
            light_.config(cfg);
            panel_.setLight(&light_);
        }
        setPanel(&panel_);
    }

  private:
    lgfx::Panel_ST7789 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
};

Panel g_panel;
bool g_up = false;

/* Boot animation frames, decoded into PSRAM. The cap is a memory budget: each
 * frame is a full 320x240 at 16 bits, so 150 KB, and sixteen of them is 2.4 MB
 * of the 8 MB PSRAM. A directory with more than this in it plays the first
 * sixteen rather than failing. */
const int MAX_FRAMES = 16;
/* Not NAME_MAX: that is a system macro, and naming a constant after it here
 * expands into nonsense at the point of use. */
const size_t FRAME_NAME_MAX = 64;

lgfx::LGFX_Sprite *g_frames[MAX_FRAMES];
int g_frame_count = 0;

/* True for the extensions LovyanGFX can decode. */
bool decodable(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg") ||
           !strcasecmp(dot, ".png") || !strcasecmp(dot, ".qoi");
}

bool decode_into(lgfx::LGFX_Sprite *sprite, const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    if (!strcasecmp(dot, ".png"))
        return sprite->drawPngFile((fs::FS &)SD_MMC, path, 0, 0);
    if (!strcasecmp(dot, ".qoi"))
        return sprite->drawQoiFile((fs::FS &)SD_MMC, path, 0, 0);
    return sprite->drawJpgFile((fs::FS &)SD_MMC, path, 0, 0);
}

void free_frames(void)
{
    for (int i = 0; i < g_frame_count; i++) {
        delete g_frames[i];
        g_frames[i] = nullptr;
    }
    g_frame_count = 0;
}

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

void catnip_display_dot(int cx, int cy, int r, uint16_t rgb565)
{
    if (!g_up) return;
    g_panel.fillCircle(cx, cy, r, rgb565);
}

void catnip_display_fill(uint16_t rgb565)
{
    if (g_up) g_panel.fillScreen(rgb565);
}

int catnip_display_load_frames(const char *dir)
{
    if (!g_up || !dir || !dir[0]) return 0;

    File folder = SD_MMC.open(dir);
    if (!folder || !folder.isDirectory()) {
        Serial.printf("[catnip] boot frames: %s is not a directory\n", dir);
        return 0;
    }

    /* Collect the names first and sort them, because a directory hands them
     * back in whatever order the filesystem stored them - which is not the
     * order the frames were meant to play in. */
    static char names[MAX_FRAMES][FRAME_NAME_MAX];
    int found = 0;
    for (File f = folder.openNextFile(); f; f = folder.openNextFile()) {
        if (f.isDirectory()) continue;
        const char *name = strrchr(f.name(), '/');
        name = name ? name + 1 : f.name();
        if (!decodable(name) || strlen(name) >= FRAME_NAME_MAX) continue;
        if (found == MAX_FRAMES) {
            Serial.printf("[catnip] boot frames: more than %d, using the first %d\n",
                          MAX_FRAMES, MAX_FRAMES);
            break;
        }
        strcpy(names[found++], name);
    }
    folder.close();
    if (!found) return 0;

    for (int i = 1; i < found; i++) {
        char key[FRAME_NAME_MAX];
        strcpy(key, names[i]);
        int j = i - 1;
        while (j >= 0 && strcmp(names[j], key) > 0) {
            strcpy(names[j + 1], names[j]);
            j--;
        }
        strcpy(names[j + 1], key);
    }

    free_frames();
    for (int i = 0; i < found; i++) {
        char path[CATNIP_CONFIG_PATH_MAX + FRAME_NAME_MAX + 2];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);

        lgfx::LGFX_Sprite *sprite = new lgfx::LGFX_Sprite(&g_panel);
        sprite->setPsram(true);
        sprite->setColorDepth(16);
        if (!sprite->createSprite(CATNIP_SCREEN_W, CATNIP_SCREEN_H)) {
            Serial.printf("[catnip] boot frames: out of memory at frame %d\n", i);
            delete sprite;
            break;
        }
        if (!decode_into(sprite, path)) {
            Serial.printf("[catnip] boot frames: could not decode %s\n", path);
            delete sprite;
            continue; /* one unreadable frame should not cost the others */
        }
        g_frames[g_frame_count++] = sprite;
    }

    if (g_frame_count) {
        Serial.printf("[catnip] boot frames: %d loaded from %s\n", g_frame_count, dir);
    } else {
        Serial.printf("[catnip] boot frames: nothing usable in %s\n", dir);
    }
    return g_frame_count;
}

void catnip_display_show_frame(int index)
{
    if (!g_up || index < 0 || index >= g_frame_count) return;
    g_frames[index]->pushSprite(0, 0);
}
