/*
 * poc_display.cpp - the smallest thing that should light this panel.
 *
 * Built only by `-e poc`, which excludes every other source file. It does what
 * the vendor's BSP does before its first draw and nothing else: bring up I2C,
 * configure the expander exactly as its library would, initialise the panel
 * with the vendor's configuration, and fill the screen.
 *
 * It does not light the panel, and that is why it is kept. Everything it
 * reports is correct - the rails are up, the expander answers, the
 * chip-select reads low, the panel configuration matches the vendor's field
 * for field, on the same library version - and the screen stays dark anyway.
 * Whatever is wrong is not visible from software, so this is the starting
 * point for someone with a meter: check Display_3V3 and BL_A at J1.
 *
 * One trap it exists to document: the PCA9557 sits on an always-on rail and
 * keeps its registers across an ESP32 reset. Read-modify-write inherits
 * whatever the last firmware left behind, which silently invalidated several
 * earlier experiments. Write absolute values.
 */
#include <Arduino.h>
#include <Wire.h>
#include <LovyanGFX.hpp>

static const int PIN_SDA = 1, PIN_SCL = 2;
static const uint8_t ADDR_IOEXP = 0x19;
static const uint8_t ADDR_PMU   = 0x34;

class Panel : public lgfx::LGFX_Device {
public:
    Panel()
    {
        { auto c = bus_.config();
          c.spi_host = SPI2_HOST; c.spi_mode = 0;
          c.freq_write = 80000000; c.freq_read = 16000000;
          c.spi_3wire = false; c.use_lock = true;
          c.dma_channel = SPI_DMA_CH_AUTO;
          c.pin_sclk = 41; c.pin_mosi = 40; c.pin_miso = -1; c.pin_dc = 39;
          bus_.config(c); panel_.setBus(&bus_); }
        { auto c = panel_.config();
          c.pin_cs = -1; c.pin_rst = -1; c.pin_busy = -1;
          c.panel_width = 240; c.panel_height = 320;
          c.offset_x = 0; c.offset_y = 0; c.offset_rotation = 3;
          c.dummy_read_pixel = 8; c.dummy_read_bits = 1;
          c.readable = false; c.invert = true; c.rgb_order = false;
          c.dlen_16bit = false; c.bus_shared = false;
          panel_.config(c); }
        { auto c = light_.config();
          c.pin_bl = 42; c.invert = false; c.freq = 44100; c.pwm_channel = 7;
          light_.config(c); panel_.setLight(&light_); }
        setPanel(&panel_);
    }
private:
    lgfx::Panel_ST7789 panel_;
    lgfx::Bus_SPI      bus_;
    lgfx::Light_PWM    light_;
};

static Panel lcd;

static bool wr(uint8_t addr, uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(addr); Wire.write(reg); Wire.write(val);
    return Wire.endTransmission() == 0;
}
static bool rd(uint8_t addr, uint8_t reg, uint8_t *out)
{
    Wire.beginTransmission(addr); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, 1) != 1) return false;
    *out = Wire.read();
    return true;
}

/* What the expander library does: read-modify-write, one bit at a time. */
static void exp_pin_output(uint8_t pin)
{
    uint8_t cfg = 0xFF;
    rd(ADDR_IOEXP, 0x03, &cfg);
    cfg &= (uint8_t)~(1u << pin);
    wr(ADDR_IOEXP, 0x03, cfg);
}
static void exp_pin_write(uint8_t pin, bool high)
{
    uint8_t out = 0x00;
    rd(ADDR_IOEXP, 0x01, &out);
    if (high) out |= (uint8_t)(1u << pin); else out &= (uint8_t)~(1u << pin);
    wr(ADDR_IOEXP, 0x01, out);
}

void setup()
{
    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 8000) delay(10);
    Serial.println("[poc] start");

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);
    delay(200);

    /* The expander is on LDO4; without it there is nothing to talk to. */
    uint8_t rails = 0;
    if (rd(ADDR_PMU, 0x12, &rails)) {
        Serial.printf("[poc] rails=0x%02X\n", rails);
        if (!(rails & 0x02)) { wr(ADDR_PMU, 0x27, 104); wr(ADDR_PMU, 0x12, rails | 0x02); delay(50); }
    } else {
        Serial.println("[poc] no PMIC");
    }

    uint8_t probe = 0;
    Serial.printf("[poc] expander at 0x19: %s\n",
                  rd(ADDR_IOEXP, 0x00, &probe) ? "present" : "ABSENT");

    /* Absolute values, not read-modify-write. The expander is a separate chip
     * on an always-on rail: its registers survive an ESP32 reset, so whatever
     * the previously flashed firmware left behind is still there. Earlier
     * experiments left every pin an output driving high, and each later test
     * silently inherited that instead of the vendor's configuration.
     *
     * 0xFC: IO0 and IO1 outputs, the rest inputs, which is where a freshly
     * powered expander plus the vendor's two pinMode calls would land.
     * 0x02: IO0 (chip-select) low, IO1 (amplifier enable) high. */
    wr(ADDR_IOEXP, 0x01, 0x02);
    wr(ADDR_IOEXP, 0x03, 0xFC);

    uint8_t cfg = 0, out = 0;
    rd(ADDR_IOEXP, 0x03, &cfg); rd(ADDR_IOEXP, 0x01, &out);
    Serial.printf("[poc] expander cfg=0x%02X out=0x%02X\n", cfg, out);

    Serial.println("[poc] lcd.init()");
    lcd.init();
    Serial.println("[poc] filling RED");
    lcd.fillScreen(0xF800);
    Serial.println("[poc] done");
}

void loop()
{
    static uint32_t n = 0;
    static const uint16_t C[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    static const char *N[] = {"RED", "GREEN", "BLUE", "WHITE"};
    lcd.fillScreen(C[n % 4]);

    /* Report the state every pass rather than once at boot. The boot log keeps
     * being missed - the port re-enumerates, and a reset does not always land
     * while this loop is running - so put the facts where they can always be
     * read. */
    uint8_t rails = 0, cfg = 0, out = 0, in = 0;
    bool pmu = rd(ADDR_PMU, 0x12, &rails);
    bool exp = rd(ADDR_IOEXP, 0x03, &cfg);
    rd(ADDR_IOEXP, 0x01, &out);
    rd(ADDR_IOEXP, 0x00, &in);
    Serial.printf("[poc] %-5s | pmu=%d rails=0x%02X | exp=%d cfg=0x%02X out=0x%02X in=0x%02X\n",
                  N[n % 4], (int)pmu, rails, (int)exp, cfg, out, in);
    n++;
    delay(2000);
}
