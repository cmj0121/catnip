/*
 * board.h - MeowKit hardware facts: which pin is wired to what.
 *
 * These are properties of the board, not of any particular firmware, and they
 * are what every driver here has to agree on. Keeping them in one header means
 * a wiring correction lands in a single place instead of being chased through
 * the drivers.
 */
#ifndef CATNIP_BOARD_H
#define CATNIP_BOARD_H

/* Power rail. The board latches its own supply: PWR_HOLD must be driven high
 * and kept there, or the device switches itself off shortly after boot. */
#define CATNIP_PIN_PWR_HOLD 11
#define CATNIP_PIN_PWR_ON   10

/* ST7789 panel, SPI. Chip-select and reset are not MCU pins - they sit on the
 * I/O expander below - and MISO is not connected, so the panel is write-only. */
#define CATNIP_PIN_LCD_MOSI 40
#define CATNIP_PIN_LCD_MISO -1
#define CATNIP_PIN_LCD_SCLK 41
#define CATNIP_PIN_LCD_DC   39
#define CATNIP_PIN_LCD_CS   -1
#define CATNIP_PIN_LCD_RST  -1
#define CATNIP_PIN_LCD_BL   42

/* The panel is a 240x320 part mounted sideways; rotation 3 presents it as the
 * 320x240 landscape screen the user actually sees. It also inverts. */
#define CATNIP_LCD_PANEL_W    240
#define CATNIP_LCD_PANEL_H    320
#define CATNIP_LCD_ROTATION   3
#define CATNIP_LCD_INVERT     true
#define CATNIP_LCD_RGB_ORDER  false
#define CATNIP_LCD_SPI_HZ     80000000
#define CATNIP_LCD_BL_PWM_HZ  44100
/* The backlight enable is active-low: driving IO42 low lights the panel and
 * a 100% PWM duty switches it off. Found by cycling the pin by hand and
 * watching the screen; the published source says invert = false, and with
 * that setting "brightness 0" at boot is full on and "brightness 255" is off,
 * which is how a working panel looked dead. */
#define CATNIP_LCD_BL_INVERT  true

/* Screen size as oriented for the user. */
#define CATNIP_SCREEN_W 320
#define CATNIP_SCREEN_H 240

/* Status LED: an XL-2121RGBC-2812B, a WS2812 style addressable RGB part on a
 * single data line. It cannot be driven by holding the pin high or low - it
 * waits for a timed one-wire frame and stays dark for anything else, which
 * looks exactly like a dead LED.
 *
 * The pin is IO38, per the interconnect sheet. The vendor firmware's LED app
 * uses GPIO8, which on this board is a pin on the expansion header, not the
 * LED; following that source is how this was wrong the first time.
 *
 * A second, separate LED hangs off the PMIC's CHGLED pin. That is the charge
 * indicator and is not ours to drive. */
#define CATNIP_PIN_LED 38

/* microSD, 1-bit SDMMC. */
#define CATNIP_PIN_SD_CLK 47
#define CATNIP_PIN_SD_CMD 48
#define CATNIP_PIN_SD_D0  21

/* Supply rails, all from the AXP173 PMIC. Which rail a peripheral sits on is
 * the difference between "broken" and "not switched on yet":
 *
 *   DCDC1  (1.2A)  the ESP32 itself, the display, the WS2812, audio
 *   LDO4   (500mA) the I/O expander, NFC
 *   LDO3   (200mA) the SD card
 *   LDO2   (200mA) the sensors
 *
 * The MCU runs off DCDC1, so DCDC1 is necessarily up whenever any code is
 * executing - and so are the display and the LED. The others are not implied,
 * and an LDO that is off makes its peripheral simply absent from the bus.
 *
 * PCA9557 I/O expander. The panel's chip-select and reset are not MCU pins at
 * all - they hang off this expander, which is why CATNIP_PIN_LCD_CS and
 * CATNIP_PIN_LCD_RST are -1.
 *
 * The map below is NOT the one in the vendor's published devices.cpp (IO0 =
 * chip-select, IO1 = speaker enable). That source describes an older board
 * revision. On the unit that ships, the stock firmware - recovered by
 * disassembling the flash backup in backup/ - drives the expander like this:
 *
 *   IO1  LCD_CS   high while the panel is reset, then low and left low
 *   IO0  reset    pulsed low, then held high a while before CS drops
 *   IO6  reset    pulsed together with IO0 (most likely the touch controller)
 *   IO3  ?        driven low and left low; purpose unknown, so mirror it
 *
 * The speaker enable (PA_EN, IO1 in the published source) is not among them;
 * where it went on this revision is not yet known.
 *
 * Following the published map drives CS high and holds the panel in reset at
 * the same time, and the screen stays dark looking exactly like a dead
 * backlight. Sweeping one pin at a time cannot find this: CS and reset have to
 * be right together before the panel answers anything.
 *
 * The expander answers at 0x19. Do not be tempted by 0x18 turning up in a bus
 * scan: that is the ES8311 audio codec, and writing expander registers to it
 * configures the wrong chip while the panel stays dark. If 0x19 is absent the
 * expander is unpowered rather than misaddressed: the peripheral rail is not
 * up merely because the CPU is running.
 */
#define CATNIP_I2C_ADDR_PMU    0x34
#define CATNIP_I2C_ADDR_IOEXP  0x19
#define CATNIP_IOEXP_LCD_RST   0
#define CATNIP_IOEXP_LCD_CS    1
#define CATNIP_IOEXP_IO3       3
#define CATNIP_IOEXP_AUX_RST   6

/* Reset timing, as the stock firmware does it. */
#define CATNIP_IOEXP_RST_LOW_MS    20
#define CATNIP_IOEXP_RST_HIGH_MS   120
#define CATNIP_IOEXP_CS_SETTLE_MS  5

/* Shared I2C bus: RTC, IMU, touch, audio codecs. */
#define CATNIP_PIN_I2C_SCL 2
#define CATNIP_PIN_I2C_SDA 1
#define CATNIP_I2C_HZ      400000

#endif /* CATNIP_BOARD_H */
