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

/* PWR_ON above is the vendor's name for GPIO10, and the power button really
 * does not reach the MCU: GPIO10 reads high whether or not the button is down.
 * Pressing it sets the AXP173's power-key interrupt instead - register 0x46,
 * bit 1 - which is how catnip_pmu_power_key_pressed() sees it. Measured by
 * logging the pin and the PMIC's interrupt registers together while the button
 * was pressed, and confirmed again during the input probe run below.
 *
 * This paragraph used to say the same of GPIO5 and GPIO4, the published
 * buttons A and B, and that conclusion was wrong: the switch on GPIO4 works
 * perfectly well. The earlier reading was taken with INPUT_PULLUP against the
 * board's own external pull-up - the same direction - so pressed and released
 * looked identical on a pin that was doing exactly what it should.
 *
 * The published A and B numbers are wrong too, and not merely mislabelled: the
 * switch on GPIO4 is B, A is on GPIO6, and GPIO5 is the joystick's centre
 * press. As with the display, the vendor's map describes some other board.
 * The measured map is below. */

/* Physical switches: a five-way joystick and buttons A and B. Every one of
 * them is a plain MCU GPIO, active-low, with an external pull-up already
 * populated on the board. Pressing a switch pulls its pin hard to ground, so
 * the reading falls 1 -> 0 under INPUT_PULLUP and under INPUT_PULLDOWN alike,
 * and that pin's ADC falls 4095 -> 0 at the same instant.
 *
 * Nothing here is on the I/O expander - its input port read 0xB1 throughout
 * the probe run and never once changed - and there is no resistor ladder: no
 * intermediate analog value ever appeared, the ADC only ever followed the same
 * pin being grounded. So these are seven independent digital reads, and
 * polling them costs no I2C traffic.
 *
 * These pins are known because they were counted, not because anyone watched
 * the order events arrived in. That distinction is the whole reason this map
 * can be trusted, and it is worth a paragraph, because the first version of it
 * was wrong.
 *
 * The first attempt inferred the mapping from the sequence the probe's log
 * printed presses in. But the log arrives over this chip's native USB CDC,
 * which drops out under load: the reader takes a SerialException, reopens the
 * port, and everything printed in between is lost. One capture lost four
 * consecutive snapshots and three [press] lines while keeping their [release]
 * partners - so the sequence that was reasoned over had never happened, and it
 * put A and B the wrong way round.
 *
 * The probe was changed to keep a running count of debounced presses per pin
 * and reprint the whole table every three seconds. A total is self-correcting:
 * a print that never arrives costs a three-second wait and nothing else. The
 * settling measurement pressed three switches a different number of times, so
 * that the counts themselves are the labels and no ordering is needed:
 *
 *   button A pressed 3 times   ->  gpio6 counted 3
 *   button B pressed 7 times   ->  gpio4 counted 7
 *   centre   pressed 5 times   ->  nothing anywhere counted 5
 *
 * repeated identically across many prints. The four directions were confirmed
 * by name in an earlier run and are unchanged.
 *
 * GPIO7, GPIO43 and GPIO46 each counted exactly 1 and none of them counted 5.
 * All three sit at a hard low at rest, and a single count on a pin that starts
 * low is the filter settling at startup, not a press. There is nothing there
 * to chase.
 *
 * The joystick's centre press is the switch that never responded - not B, as
 * this comment used to say. Across five presses in the counted run and roughly
 * ten in the runs before it, nothing in the scanned set moved, and the scanned
 * set by then included GPIO0 and GPIO10. GPIO5 is externally pulled high
 * exactly like the six confirmed switch pins, so the net and its pull-up are
 * populated and the contact is simply not closing. The number below is
 * therefore inference from that pull-up and from GPIO5 being what is left
 * over: it is NOT a measurement, and nobody should read it as one. This is a
 * single unit, so it may be a fault on this board rather than a property of
 * the design; a second board would settle it. */
#define CATNIP_PIN_BTN_UP    12
#define CATNIP_PIN_BTN_DOWN  18
#define CATNIP_PIN_BTN_LEFT  17
#define CATNIP_PIN_BTN_RIGHT 8
#define CATNIP_PIN_BTN_A     6
#define CATNIP_PIN_BTN_B     4
/* Presumed, never observed to change under any pull - see above. */
#define CATNIP_PIN_BTN_CENTRE 5

/* Contact bounce is real here and has to be filtered out. One press of the
 * switch on GPIO4 - button B, as the counting later established - was logged
 * as: down at t=255966, up at t=256792, down again at t=256822, 30 ms after
 * the release, and up again at t=256982. That third edge is the contact
 * bouncing, not a second press. Clean presses in the same runs lasted 125-310
 * ms, so a 50 ms window is wide enough to cover the bounce that was seen and
 * far too narrow to swallow a real press. */
#define CATNIP_INPUT_DEBOUNCE_MS 50

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
#define CATNIP_LCD_PANEL_W   240
#define CATNIP_LCD_PANEL_H   320
#define CATNIP_LCD_ROTATION  3
#define CATNIP_LCD_INVERT    true
#define CATNIP_LCD_RGB_ORDER false
#define CATNIP_LCD_SPI_HZ    80000000
#define CATNIP_LCD_BL_PWM_HZ 44100
/* The backlight enable is active-low: driving IO42 low lights the panel and
 * a 100% PWM duty switches it off. Found by cycling the pin by hand and
 * watching the screen; the published source says invert = false, and with
 * that setting "brightness 0" at boot is full on and "brightness 255" is off,
 * which is how a working panel looked dead. */
#define CATNIP_LCD_BL_INVERT true

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
 *   IO6  reset    pulsed together with IO0 (purpose unknown - see the bus
 *                  scan below, which does not support the touch-reset guess)
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
#define CATNIP_I2C_ADDR_PMU   0x34
#define CATNIP_I2C_ADDR_IOEXP 0x19
#define CATNIP_IOEXP_LCD_RST  0
#define CATNIP_IOEXP_LCD_CS   1
#define CATNIP_IOEXP_IO3      3
#define CATNIP_IOEXP_AUX_RST  6

/* Reset timing, as the stock firmware does it. */
#define CATNIP_IOEXP_RST_LOW_MS   20
#define CATNIP_IOEXP_RST_HIGH_MS  120
#define CATNIP_IOEXP_CS_SETTLE_MS 5

/* Shared I2C bus: RTC, IMU, touch, audio codecs. Everything that answered a
 * scan of it, from the probe's boot sequence:
 *
 *   0x19  PCA9557 I/O expander    identified above
 *   0x34  AXP173 PMIC             identified above
 *   0x18  ES8311 audio codec      identified above
 *   0x38  FT6336 touch controller confirmed by its identity registers:
 *                                 0xA3 = 0x64 and 0xA8 = 0x11, which is what
 *                                 an FT6336 reports
 *   0x51  likely a PCF8563 / BM8563 RTC
 *   0x68  likely an IMU
 *   0x41  unknown
 *
 * The last three are addresses that were seen answering, nothing more. 0x51
 * and 0x68 are the conventional addresses for an RTC and an IMU, which is
 * consistent with what this bus is supposed to carry, but neither chip has
 * been identified by reading a register, and 0x41 has no guess attached at
 * all. Do not let the plausible names harden into facts the way the vendor's
 * pin map did.
 *
 * All three scans - at reset, after the PMIC bring-up, and after the expander
 * bring-up - returned identical lists. The touch controller was answering
 * before the expander released anything, so the standing guess that expander
 * IO6 is the touch reset is not supported by this run. It is not disproven
 * either: the capture was a reflash of a device that was already running, so
 * the rails were up and nothing was actually cold. A cold boot with the
 * battery pulled could look different, and what IO6 does remains unknown. */
#define CATNIP_PIN_I2C_SCL    2
#define CATNIP_PIN_I2C_SDA    1
#define CATNIP_I2C_HZ         400000
#define CATNIP_I2C_ADDR_TOUCH 0x38

#endif /* CATNIP_BOARD_H */
