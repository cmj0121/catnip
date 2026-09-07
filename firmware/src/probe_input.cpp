/*
 * probe_input.cpp - find out which pin every switch is actually on.
 *
 * Built only by `-e probe`, which excludes the rest of the firmware. It has
 * been run three times and has now answered the question it was written for:
 * the pin map in board.h is what its tallies counted. What it found on the way
 * is why it looks the way it does. The first run read the four joystick
 * directions correctly and got A and B the wrong way round, because the
 * mapping was inferred from the order presses appeared in and the link drops
 * lines. The second widened the pin list and made the log readable at the
 * device. The third replaced order with counting, and that settled it: press a
 * switch three times, five times, seven times, and read which pin counted
 * what. One switch - the joystick's centre press - has still never moved
 * anything, and finding it, if it can be found, is what this sketch is still
 * for. Its output:
 *
 *   [press] 12043 A              a switch board.h can name, debounced
 *   [longpress] 12643 A          still held after LONG_PRESS_MS
 *   [release] 12871 A
 *   [touch] 13022 down (118, 205)      the controller's own raw coordinates
 *   [imu] 13500 ax=-0.02 ...   gravity, and the screen edge it implies
 *   [probe] 13440 gpio7 pullup 1->0    a pin board.h cannot name yet
 *   [tally] 45210                      the running press count, every pin
 *
 * The raw lines are the whole point of the exercise and stay exactly as they
 * were: the switch that has never been seen may turn up on GPIO0, on GPIO10,
 * or on a pin nobody has suspected, and a tidier log must not be what hides
 * it.
 *
 * The tally block is the one that is meant to be trusted, and the reason is
 * the link rather than the board. This chip's native USB CDC drops out under
 * load: the host reader takes a SerialException, reopens the port, and
 * everything printed in between is gone. One capture lost four consecutive
 * snapshots and, worse, arrived with three [release] lines whose [press]
 * partners had been dropped - so the pin map inferred from the order events
 * appeared in was inferred from a sequence that was never real, and it was
 * wrong. Any reading that depends on a particular line surviving is unsound
 * here. A cumulative count does not: it is reprinted every few seconds and
 * each print carries the whole truth, so a block that goes missing costs
 * nothing but a wait. Press a button three times and look for the pin that
 * says 3 - no ordering, no timestamps, no single line that has to arrive.
 *
 * The second question this sketch is now asked is which way is up, and the
 * [imu] block answers it under the same rule as the tally. Orientation is a
 * level rather than an edge: the device is in some attitude for as long as it
 * is held there, so reprinting the current reading every half second means a
 * dropped line costs half a second and nothing else. Nothing about the answer
 * has to be reconstructed from a particular line arriving.
 *
 * What the [imu] block cannot do is tell you which screen edge an axis
 * belongs to. The part is mounted however it was mounted, and no datasheet
 * says how; the edge name printed at the end of each line is a GUESS, marked
 * as one, and turning the device four ways is what settles it. The axis
 * numbers are printed alongside precisely so that a wrong guess is correctable
 * from the log rather than from another run.
 *
 * The named half is put through the firmware's own input_debounce module
 * rather than a filter written here, so this run doubles as the first field
 * test of the filter - which was written from the first run's timings and has
 * so far only ever seen them replayed on a host.
 *
 * It reports over USB serial and never touches the panel. The display needs
 * the expander, the expander is one of the things under test, and a probe that
 * can lose its own output is worse than none.
 *
 * How to read the log: press one switch a known number of times, then read the
 * next [tally] block and see which pin counted that many. The event and
 * transition lines are still printed as they happen and are still worth
 * having when they arrive, but nothing has to be reconstructed from them.
 *
 * The full pin snapshot is printed once at startup and never again. It was
 * twenty lines every five seconds, which is the bulk of what was crowding the
 * link that keeps dropping - and the tally now says everything about the pins
 * that repeating it was for.
 */
#include <Arduino.h>
#include <Wire.h>

#include <stdlib.h> /* labs() */

#include "device/board.h"
#include "device/i2cbus.h"
#include "device/input_debounce.h"
#include "device/ioexp.h"
#include "device/pmu.h"
#include "device/power.h"

namespace {

/* Pins the probe is allowed to touch. This is an allow-list on purpose, and
 * the safety-critical half is what it leaves out: the module is octal PSRAM,
 * and GPIO 26-32 carry the SPI flash and 33-37 the PSRAM, so reading or
 * re-moding any of them crashes or corrupts the chip that is running this.
 * They are never named below and must never be added.
 *
 * Also off the list: 19 and 20, which are USB D- and D+ and therefore the
 * serial link this log arrives on, and everything board.h already accounts for
 * - I2C on 1 and 2, the power latch on 11, the SD card on 21, 47 and 48, the
 * LED on 38 and the panel on 39 to 42.
 *
 * GPIO11 is the one exclusion that is a safety matter rather than a judgement,
 * and it must never be added: it is PWR_HOLD, an output the firmware drives
 * high to latch the board's own supply on. Re-moding it to an input to take a
 * reading releases that latch, and the device switches off in the middle of
 * the measurement.
 *
 * 0 and 10 were excluded on the first pass and are on the list for this one,
 * because the reasons for leaving them out do not survive what the first run
 * found:
 *
 *   GPIO0 was left out as the boot-mode strapping pin. Strapping is latched at
 *   reset and never consulted again, so reading the pin afterwards decides
 *   nothing - and GPIO0 is the conventional button pin on a great many boards,
 *   which makes it a first-rank suspect for the switch that has not been
 *   found. Nothing here drives it.
 *
 *   GPIO10 was left out because board.h said it reads high whether or not the
 *   power button is down. That claim came from the same measurement that was
 *   wrong about GPIO4 - INPUT_PULLUP against an external pull-up, where
 *   pressed and released look identical - so it is no longer good enough to
 *   exclude a pin with. Reading it costs nothing and, again, nothing here
 *   drives it. The power button reaching the PMIC does not mean this pin
 *   carries nothing else.
 *
 * Two groups on the list need their own justification, and the same rule
 * covers both times it comes up:
 *
 *   3, 45, 46 are strapping pins, but strapping is latched at reset and never
 *   looked at again, so reading one afterwards decides nothing. They are read
 *   and only ever read. GPIO45 in particular is the pin that chose the VDD_SPI
 *   voltage back at reset: driving it is not a way to change that, only a way
 *   to fight whatever holds it, so nothing here may ever write to it.
 *
 *   43 and 44 are U0TXD and U0RXD, but this build sets ARDUINO_USB_CDC_ON_BOOT
 *   and ARDUINO_USB_MODE, so the log goes over the chip's USB-Serial/JTAG and
 *   UART0 carries nothing. The pins are free, and a switch can be on either.
 *
 * What is left is every pin that could plausibly hold a button. */
const uint8_t ALLOWED_PINS[] = {0,  3,  4,  5,  6,  7,  8,  9,  10, 12,
                                13, 14, 15, 16, 17, 18, 43, 44, 45, 46};
const size_t PIN_COUNT = sizeof(ALLOWED_PINS) / sizeof(ALLOWED_PINS[0]);

/* A five-way stick as a resistor ladder on one pin is a real possibility on
 * this class of board, and a digital-only sweep would never see it, so the raw
 * analog value is logged alongside the digital reads wherever there is one to
 * read. Not every allowed pin has one: the ESP32-S3's converters reach GPIO1
 * to 10 (ADC1) and 11 to 20 (ADC2), so 3-10 and 12-18 are sampled while GPIO0
 * and 43 to 46 are not. ADC2 is only unusable while Wi-Fi is on, and nothing
 * here starts Wi-Fi. Floating inputs wander, so a change smaller than this is
 * noise and not printed. */
const int ADC_CHANGE_THRESHOLD = 256;

/* Stored instead of a reading for a pin with no converter behind it. A pin
 * that cannot be measured has to look different in the log from one measuring
 * zero, or the log invites a conclusion the hardware never supported. */
const int ADC_NONE = -1;

bool pin_has_adc(uint8_t pin)
{
    /* GPIO0 is below ADC1's first channel. analogRead() on it returns a number
     * that looks like a measurement and is not, which is exactly the kind of
     * thing this log must not invite anyone to reason from. */
    return pin >= 1 && pin <= 20;
}

/* Time for the internal pull to settle after a mode change before the pin is
 * read. The pulls are around 45 kohm against a few tens of picofarads; this
 * is generous. */
const unsigned SETTLE_US = 50;

/* How often the running press counts are printed, whether or not anything
 * changed. Repetition is the feature: it is what makes a dropped block
 * harmless. */
const unsigned long TALLY_MS = 3000;

/* PCA9557 input port register. Bring-up drives IO0, IO1, IO3 and IO6; the
 * other four are unaccounted for, and a switch behind the expander would
 * appear here rather than on any GPIO. */
const uint8_t IOEXP_REG_INPUT = 0x00;

/* AXP173 interrupt status registers 1 through 4, at 0x44 to 0x47. The power
 * key lives in CATNIP_PMU_REG_IRQ_STATUS_3 and is the only button board.h can
 * currently account for; the rest are read too, in case something else on the
 * board reaches the MCU only through the PMIC. Bits are cleared by writing
 * them back, and the probe does so after logging them, or a press would show
 * up once and never again.
 *
 * That register and its two bits come from pmu.h rather than being spelled out
 * again here. The probe had its own copy of both, and two copies of one part's
 * register map are two things that can drift; the AXP173 driver and the probe
 * now read the same definitions. */
const uint8_t PMU_IRQ_FIRST = 0x44;
const uint8_t PMU_IRQ_COUNT = 4;

/* Where a touch controller is expected. Not assumed: the scan reports whatever
 * answers, and this address is only read if something is there. An FT6336
 * keeps its chip identity at 0xA3 and its vendor at 0xA8, and the number of
 * fingers down at 0x02; if the part is something else the identity read says
 * so, and the finger count just never moves. */
/* board.h owns the address; the registers below are the part's own map and
 * belong to whoever is talking to it.
 *
 * device/touch.cpp names these same four registers, and that duplication is
 * deliberate rather than an oversight waiting to be tidied away. Calling
 * catnip_touch_begin() from here instead would invert what the probe is for:
 * that function refuses to interpret anything that is not an FT6336, and
 * interrogating a part whose identity is not yet established is exactly this
 * sketch's job. 0x38 is only called an FT6336 because the probe read 0xA3 and
 * 0xA8 first. The risk this leaves - the two maps drifting apart - is real and
 * is recorded here rather than hidden. */
const uint8_t TOUCH_ADDR = CATNIP_I2C_ADDR_TOUCH;
const uint8_t TOUCH_REG_TD_STATUS = 0x02;
const uint8_t TOUCH_REG_CHIP_ID = 0xA3;
const uint8_t TOUCH_REG_VENDOR = 0xA8;

/* The first touch point lives in the four registers from 0x03: the high byte
 * and then the low byte of X, then the same for Y. Only the low nibble of each
 * high byte is coordinate - the bits above it are an event flag on X and a
 * touch id on Y - so they are masked off. */
const uint8_t TOUCH_REG_P1_XH = 0x03;

/* Where an IMU is expected, and how little that is worth. board.h records
 * 0x68 as "likely an IMU" and hal_meowkit.cpp names the part a QMI8658A, but
 * both of those come from the vendor's documentation, which on this board has
 * now been wrong about the display's chip-select, the display's reset and all
 * three of the published button pins. So the address is read rather than
 * assumed, exactly as 0x38 was: it is only called an FT6336 because 0xA3 and
 * 0xA8 answered 0x64 and 0x11.
 *
 * The address is spelled out here rather than added to board.h on purpose.
 * board.h holds measured facts, and until this probe has been run there is no
 * measurement of what sits at 0x68 to record.
 *
 * A QMI8658A puts its identity at register 0x00 and reports 0x05 there. The
 * other family that conventionally answers at 0x68 - the MPU-6050 and its
 * relatives - keeps its identity at 0x75 instead, so that register is read
 * too. Reading both costs two transactions and is the difference between
 * "not a QMI8658A" and "not a QMI8658A, and here is what it might be".
 *
 * Every register below the identity ones is a QMI8658A's own map, and none of
 * them is touched unless 0x00 reads 0x05. Writing configuration bytes into an
 * unidentified chip is how the display was left dark. */
const uint8_t IMU_ADDR = 0x68;
const uint8_t IMU_REG_WHO_AM_I = 0x00;
const uint8_t IMU_REG_REVISION = 0x01;
const uint8_t IMU_REG_CTRL1 = 0x02;
const uint8_t IMU_REG_CTRL2 = 0x03;
const uint8_t IMU_REG_CTRL7 = 0x08;
const uint8_t IMU_REG_ACC_X_L = 0x35;
const uint8_t IMU_REG_MPU_WHO_AM_I = 0x75;
const uint8_t QMI8658A_WHO_AM_I = 0x05;

/* The bring-up, three writes.
 *
 * CTRL1 = 0x40 clears SensorDisable, which the part boots with set: in that
 * state the internal oscillator is off, the data registers never update, and
 * the chip answers every read with the same stale bytes. That looks exactly
 * like a device lying perfectly still and is the trap this sequence exists to
 * avoid. Bit 6 alongside it turns on address auto-increment, which is what
 * lets imu_read_accel() take all six acceleration bytes in one transaction.
 *
 * CTRL2 = 0x16 is the accelerometer's own configuration: aFS = 001 for a
 * range of +-4g and aODR = 0110 for 125 Hz. +-4g rather than +-2g because
 * gravity is 1g and a device being turned over by hand swings well past 2g on
 * the way; a clipped axis would read as the wrong attitude. 125 Hz is far more
 * than a half-second reprint needs and keeps the part out of its low-rate
 * modes.
 *
 * CTRL7 = 0x01 enables the accelerometer and leaves the gyroscope off. The
 * question here is which way gravity points, and a gyroscope cannot answer
 * it. */
const uint8_t IMU_CTRL1_VALUE = 0x40;
const uint8_t IMU_CTRL2_VALUE = 0x16;
const uint8_t IMU_CTRL7_VALUE = 0x01;

/* CTRL2's aFS field, and the counts per g each of its four settings gives.
 * The scale is read back out of the chip rather than assumed from what was
 * written, so a write that did not take turns into raw counts in the log
 * instead of numbers that look like g and are not. */
const uint8_t IMU_CTRL2_AFS_SHIFT = 4;
const uint8_t IMU_CTRL2_AFS_MASK = 0x07;
const int32_t IMU_LSB_PER_G[4] = {16384, 8192, 4096, 2048};

/* Below this, no axis is dominant enough to name an edge: the device is being
 * moved, or is on a corner. Gravity is 1000 mg, so half of it is a generous
 * margin for a device merely held in the hand. */
const int32_t IMU_UP_MIN_MG = 500;

/* How often the current attitude is reprinted. A level, not an edge - see the
 * comment at the top of the file. */
const unsigned long IMU_MS = 500;

/* THE AXIS-TO-EDGE MAP BELOW IS A GUESS, in the same sense and for the same
 * reason as the handedness in touch_map.c: nothing measured supports it yet.
 *
 * What it is a guess about: the panel is a 240x320 part driven at
 * CATNIP_LCD_ROTATION 3 so the user sees a 320x240 landscape screen, and the
 * IMU is a separate package soldered somewhere on the board. The datasheet
 * says where the chip's axes are relative to its own package and nothing at
 * all about where the package is relative to the screen, so the correspondence
 * cannot be derived - only measured.
 *
 * The guess assumes the ordinary case: the part is placed square with the
 * board outline, its +X running along the panel's X axis (across the panel's
 * short edge, 0..239) and its +Y along the panel's Y axis (down the long edge,
 * 0..319), with +Z out through the front of the glass. Rotation 3 maps a panel
 * point to the screen as screen_x = PANEL_H - 1 - panel_y, screen_y = panel_x
 * - the same arithmetic touch_map.c does - so increasing panel X walks DOWN
 * the screen and increasing panel Y walks LEFT across it. Hence +X toward the
 * ceiling means the bottom edge is up, and +Y toward the ceiling means the
 * left edge is up.
 *
 * The sign convention is the accelerometer's, not gravity's: at rest the part
 * measures the force holding it up, so the axis reading positive is the one
 * pointing at the ceiling. No negation is needed anywhere and none should be
 * added.
 *
 * How to correct it: turn the device so each screen edge in turn points at the
 * ceiling and read the line. If an edge name is wrong, the fix is this table
 * and only this table - six rows, one per half-axis. Nothing downstream reads
 * the axes directly, so no sign has to be chased through the reader. */
struct UpEdge {
    uint8_t axis;
    int8_t sign;
    const char *edge;
};
const UpEdge IMU_UP_EDGE[] = {
    {0, +1, "bottom edge"},
    {0, -1, "top edge"},
    {1, +1, "left edge"},
    {1, -1, "right edge"},
    {2, +1, "screen face (lying flat, screen up)"},
    {2, -1, "back (lying flat, screen down)"},
};
const size_t IMU_UP_EDGE_COUNT = sizeof(IMU_UP_EDGE) / sizeof(IMU_UP_EDGE[0]);

/* The switches board.h can now name, and the pin each one sits on. These are
 * reported by name and put through the firmware's debounce filter; every other
 * allowed pin keeps the raw change logging, because which pin B is really on
 * is the open question this pass exists to answer. */
struct NamedButton {
    uint8_t pin;
    const char *name;
};
const NamedButton NAMED_BUTTONS[] = {
    {CATNIP_PIN_BTN_UP, "up"},         {CATNIP_PIN_BTN_DOWN, "down"},
    {CATNIP_PIN_BTN_LEFT, "left"},     {CATNIP_PIN_BTN_RIGHT, "right"},
    {CATNIP_PIN_BTN_CENTRE, "centre"}, {CATNIP_PIN_BTN_A, "A"},
    {CATNIP_PIN_BTN_B, "B"},
};
const size_t NAMED_COUNT = sizeof(NAMED_BUTTONS) / sizeof(NAMED_BUTTONS[0]);

/* How long a press has to be held to be called a long one. Reported once,
 * while the button is still down; the release is then reported as usual. */
const unsigned long LONG_PRESS_MS = 600;

/* One sample of everything. Kept as a struct so the change detection is a
 * field-by-field comparison against the previous pass. */
struct Sample {
    uint8_t pullup[PIN_COUNT];
    uint8_t pulldown[PIN_COUNT];
    int adc[PIN_COUNT];
    bool ioexp_ok;
    uint8_t ioexp_in;
    bool pmu_ok;
    uint8_t pmu_irq[PMU_IRQ_COUNT];
    bool touch_ok;
    uint8_t touch_td;
};

Sample g_prev;
bool g_touch_present = false;
unsigned long g_last_tally = 0;

/* What the probe established about 0x68 at startup, so the loop does not have
 * to re-ask it every half second. streaming stays false whenever the part was
 * not identified: an unknown chip's data registers are not data. */
struct ImuState {
    bool streaming;
    bool scale_known;
    int32_t lsb_per_g;
};
ImuState g_imu = {false, false, 0};
unsigned long g_last_imu = 0;

/* One of these per allowed pin, not per named switch: the pin a missing button
 * turns out to be on has to be counted too, and it is not known yet which pin
 * that is. Alongside the filter, what the filter deliberately does not know -
 * when the current press started and whether it has already been called long -
 * and the count that is the point of the whole block.
 *
 * One struct rather than four parallel arrays. They were always indexed
 * together and they are the state of a single pin, so keeping them apart meant
 * a new field was four declarations and every initialiser, and a missed one
 * would have desynchronised the indices with nothing to catch it. */
struct PinState {
    catnip_debounce filter;
    unsigned long down_at;
    bool long_reported;
    unsigned long press_count;
};
PinState g_pin[PIN_COUNT];

const char *button_name(uint8_t pin)
{
    for (size_t i = 0; i < NAMED_COUNT; i++) {
        if (NAMED_BUTTONS[i].pin == pin) return NAMED_BUTTONS[i].name;
    }
    return NULL;
}

/* 128 bits, one per 7-bit address. */
struct BusMap {
    uint8_t bits[16];
};

void scan_bus(BusMap *map)
{
    memset(map->bits, 0, sizeof(map->bits));
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0)
            map->bits[addr >> 3] |= (uint8_t)(1u << (addr & 7));
    }
}

bool bus_has(const BusMap *map, uint8_t addr)
{
    return (map->bits[addr >> 3] >> (addr & 7)) & 1u;
}

const char *bus_name(uint8_t addr)
{
    switch (addr) {
    case CATNIP_I2C_ADDR_PMU: return "AXP173 PMIC";
    case CATNIP_I2C_ADDR_IOEXP: return "PCA9557 expander";
    case 0x18: return "ES8311 codec";
    case TOUCH_ADDR: return "touch controller?";
    case IMU_ADDR: return "imu?";
    default: return "unknown";
    }
}

void print_bus(const char *label, const BusMap *map)
{
    Serial.printf("[probe] i2c %s:", label);
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (!bus_has(map, addr)) continue;
        Serial.printf(" 0x%02X(%s)", addr, bus_name(addr));
        found++;
    }
    if (!found) Serial.print(" (nothing responded)");
    Serial.println();
}

/* The point of scanning more than once: a chip held in reset by the expander
 * is absent from the first scan and present in the next, and that difference
 * is what says which expander pin its reset is on. */
void print_bus_diff(const char *label, const BusMap *before, const BusMap *after)
{
    Serial.printf("[probe] i2c diff %s:", label);
    int changed = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        bool was = bus_has(before, addr), now = bus_has(after, addr);
        if (was == now) continue;
        Serial.printf(" %s0x%02X(%s)", now ? "+" : "-", addr, bus_name(addr));
        changed++;
    }
    if (!changed) Serial.print(" (no change)");
    Serial.println();
}

void print_touch_identity(void)
{
    uint8_t chip = 0, vendor = 0;
    bool got_chip = catnip_i2c_read_reg(TOUCH_ADDR, TOUCH_REG_CHIP_ID, &chip);
    bool got_vendor = catnip_i2c_read_reg(TOUCH_ADDR, TOUCH_REG_VENDOR, &vendor);
    if (got_chip && got_vendor) {
        Serial.printf("[probe] 0x38 reg 0xA3=0x%02X reg 0xA8=0x%02X "
                      "(an FT6336 reports 0x64 and 0x11)\n",
                      chip, vendor);
    } else {
        Serial.println("[probe] 0x38 answered the scan but not a register read");
    }
}

/* Say what answers at 0x68, and decide from that whether anything further may
 * be read as accelerometer data. Nothing is written to the chip here: this
 * function only asks who it is. */
void print_imu_identity(void)
{
    uint8_t who = 0, rev = 0, mpu = 0;
    bool got_who = catnip_i2c_read_reg(IMU_ADDR, IMU_REG_WHO_AM_I, &who);
    bool got_rev = catnip_i2c_read_reg(IMU_ADDR, IMU_REG_REVISION, &rev);
    bool got_mpu = catnip_i2c_read_reg(IMU_ADDR, IMU_REG_MPU_WHO_AM_I, &mpu);

    if (!got_who) {
        Serial.println("[imu] 0x68 answered the scan but not a register read");
        return;
    }
    Serial.printf("[imu] 0x68 who_am_i=0x%02X", who);
    if (got_rev) Serial.printf(" reg0x01=0x%02X", rev);
    if (got_mpu) Serial.printf(" reg0x75=0x%02X", mpu);
    Serial.printf(" (a QMI8658A reports 0x%02X at 0x00)\n", QMI8658A_WHO_AM_I);

    if (who == QMI8658A_WHO_AM_I) {
        g_imu.streaming = true;
        return;
    }
    Serial.println("[imu] That is NOT what a QMI8658A reports, so this is some other");
    Serial.println("[imu] part and its registers are not read as if it were one. If");
    Serial.println("[imu] reg0x75 above reads 0x68 or 0x70 it is an MPU-6050 family");
    Serial.println("[imu] chip; either way the number is the finding, not a fault.");
}

/* Bring the accelerometer out of the disabled state it boots in, then read the
 * range back so counts can be turned into g. A write that is not acknowledged
 * is reported and not retried: what that means is worth seeing in the log. */
void imu_begin(void)
{
    uint8_t ctrl2 = 0;

    if (!catnip_i2c_write_reg(IMU_ADDR, IMU_REG_CTRL1, IMU_CTRL1_VALUE) ||
        !catnip_i2c_write_reg(IMU_ADDR, IMU_REG_CTRL2, IMU_CTRL2_VALUE) ||
        !catnip_i2c_write_reg(IMU_ADDR, IMU_REG_CTRL7, IMU_CTRL7_VALUE)) {
        Serial.println("[imu] the chip identified itself and then refused a "
                       "configuration write");
        return;
    }

    if (!catnip_i2c_read_reg(IMU_ADDR, IMU_REG_CTRL2, &ctrl2)) {
        Serial.println("[imu] CTRL2 did not read back, so the range is unknown and "
                       "the axes below are raw counts");
        return;
    }

    uint8_t afs = (uint8_t)((ctrl2 >> IMU_CTRL2_AFS_SHIFT) & IMU_CTRL2_AFS_MASK);
    if (afs > 3) {
        Serial.printf("[imu] CTRL2 read back 0x%02X, whose range field is not one this "
                      "probe knows; the axes below are raw counts\n",
                      ctrl2);
        return;
    }
    g_imu.scale_known = true;
    g_imu.lsb_per_g = IMU_LSB_PER_G[afs];
    Serial.printf("[imu] CTRL2=0x%02X, so the range is +-%dg at %ld counts per g\n",
                  ctrl2, 2 << afs, (long)g_imu.lsb_per_g);
}

/* The three acceleration axes, low byte first: six consecutive registers in
 * one transaction, which is what the address auto-increment enabled in
 * imu_begin() was for. */
bool imu_read_accel(int16_t out[3])
{
    uint8_t r[6];

    if (!catnip_i2c_read_regs(IMU_ADDR, IMU_REG_ACC_X_L, r, sizeof(r))) return false;
    for (uint8_t i = 0; i < 3; i++) {
        out[i] = (int16_t)((uint16_t)r[i * 2] | ((uint16_t)r[i * 2 + 1] << 8));
    }
    return true;
}

/* The screen edge pointing at the ceiling, or NULL when no axis is dominant
 * enough to name one. The whole mapping is the table this reads; see the
 * comment on IMU_UP_EDGE, and correct it there. */
const char *imu_up_edge(const int32_t mg[3])
{
    uint8_t axis = 0;

    for (uint8_t i = 1; i < 3; i++) {
        if (labs(mg[i]) > labs(mg[axis])) axis = i;
    }
    if (labs(mg[axis]) < IMU_UP_MIN_MG) return NULL;

    int8_t sign = mg[axis] >= 0 ? (int8_t)1 : (int8_t)-1;
    for (size_t i = 0; i < IMU_UP_EDGE_COUNT; i++) {
        if (IMU_UP_EDGE[i].axis == axis && IMU_UP_EDGE[i].sign == sign)
            return IMU_UP_EDGE[i].edge;
    }
    return NULL;
}

/* Milli-g as a signed decimal, without printf's float support. Whether %f
 * prints anything depends on how newlib was configured for this build, and a
 * log line that silently comes out useless is the one failure this whole
 * sketch is written to avoid. Integer arithmetic cannot have that problem. */
void imu_format_g(int32_t mg, char *buf, size_t n)
{
    int32_t whole = labs(mg) / 1000;
    int32_t hundredths = (labs(mg) % 1000) / 10;

    snprintf(buf, n, "%c%ld.%02ld", mg < 0 ? '-' : '+', (long)whole, (long)hundredths);
}

/* One line, reprinted every IMU_MS whether or not anything moved. The raw
 * counts are printed alongside the g values because they are what makes a
 * wrong edge name correctable without another run. */
void print_imu(unsigned long now)
{
    int16_t raw[3];
    int32_t mg[3];
    char ax[12], ay[12], az[12];
    const char *edge;

    if (!imu_read_accel(raw)) {
        Serial.printf("[imu] %lu the acceleration registers did not answer\n", now);
        return;
    }
    if (!g_imu.scale_known) {
        Serial.printf("[imu] %lu ax=%d ay=%d az=%d (RAW COUNTS, range unknown, so no "
                      "edge is derived)\n",
                      now, raw[0], raw[1], raw[2]);
        return;
    }

    for (uint8_t i = 0; i < 3; i++) {
        mg[i] = ((int32_t)raw[i] * 1000) / g_imu.lsb_per_g;
    }
    imu_format_g(mg[0], ax, sizeof(ax));
    imu_format_g(mg[1], ay, sizeof(ay));
    imu_format_g(mg[2], az, sizeof(az));
    edge = imu_up_edge(mg);
    Serial.printf("[imu] %lu ax=%s ay=%s az=%s g (raw %d %d %d)  ->  up = %s (GUESS)\n",
                  now, ax, ay, az, raw[0], raw[1], raw[2],
                  edge ? edge : "unclear, no axis dominant (moving?)");
}

/* Read the first touch point: four consecutive registers in one transaction,
 * so the X and Y come out of the part at the same instant rather than being
 * assembled from four separate reads. */
bool read_touch_point(uint16_t *x, uint16_t *y)
{
    uint8_t r[4];

    if (!catnip_i2c_read_regs(TOUCH_ADDR, TOUCH_REG_P1_XH, r, sizeof(r))) return false;
    *x = (uint16_t)(((r[0] & 0x0F) << 8) | r[1]);
    *y = (uint16_t)(((r[2] & 0x0F) << 8) | r[3]);
    return true;
}

/* Read every pin under both pulls, then its analog value. The order matters:
 * analogRead leaves the pin in analog mode, so the digital reads must come
 * first and the next pass must set the mode again. All of these pins are
 * unassigned according to board.h, which is why re-moding them every pass is
 * acceptable here and would not be on a pin a peripheral depends on.
 *
 * The bare INPUT before the analog read is not redundant tidying, so do not
 * remove it: the internal pull is about 45 kohm, a resistor ladder here would
 * be a few kohm, and reading the pin with the pulldown still engaged loads
 * the divider and drags every step of the ladder toward zero until the
 * directions are no longer distinguishable. On a pin that is simply floating
 * it pins the value near zero, which reads as "nothing here" for a pin that is
 * unconnected either way. The analog measurement is the one that would find an
 * analog stick, so it is taken with nothing pulling on the pin.
 *
 * Pins with no converter behind them are recorded as ADC_NONE rather than
 * read: analogRead on one of those returns a number that looks like a
 * measurement and is not. */
void sample_pins(Sample *s)
{
    for (size_t i = 0; i < PIN_COUNT; i++) {
        uint8_t pin = ALLOWED_PINS[i];
        pinMode(pin, INPUT_PULLUP);
        delayMicroseconds(SETTLE_US);
        s->pullup[i] = (uint8_t)digitalRead(pin);
        pinMode(pin, INPUT_PULLDOWN);
        delayMicroseconds(SETTLE_US);
        s->pulldown[i] = (uint8_t)digitalRead(pin);
        if (!pin_has_adc(pin)) {
            s->adc[i] = ADC_NONE;
            continue;
        }
        pinMode(pin, INPUT);
        delayMicroseconds(SETTLE_US);
        s->adc[i] = analogRead(pin);
    }
}

void sample_i2c(Sample *s)
{
    s->ioexp_ok =
        catnip_i2c_read_reg(CATNIP_I2C_ADDR_IOEXP, IOEXP_REG_INPUT, &s->ioexp_in);

    s->pmu_ok = true;
    for (uint8_t i = 0; i < PMU_IRQ_COUNT; i++) {
        s->pmu_ok = s->pmu_ok &&
                    catnip_i2c_read_reg(CATNIP_I2C_ADDR_PMU, (uint8_t)(PMU_IRQ_FIRST + i),
                                        &s->pmu_irq[i]);
    }

    s->touch_ok = g_touch_present &&
                  catnip_i2c_read_reg(TOUCH_ADDR, TOUCH_REG_TD_STATUS, &s->touch_td);
}

void print_snapshot(const Sample *s, unsigned long now)
{
    Serial.printf("[probe] %lu snapshot\n", now);
    for (size_t i = 0; i < PIN_COUNT; i++) {
        char adc[8];
        char named[16];
        const char *name = button_name(ALLOWED_PINS[i]);
        if (s->adc[i] == ADC_NONE) snprintf(adc, sizeof(adc), " no adc");
        else snprintf(adc, sizeof(adc), "%4d", s->adc[i]);
        if (name) snprintf(named, sizeof(named), "  (%s)", name);
        else named[0] = '\0';
        Serial.printf("[probe]   gpio%-2u pullup=%u pulldown=%u adc=%s%s\n",
                      ALLOWED_PINS[i], s->pullup[i], s->pulldown[i], adc, named);
    }
    if (s->ioexp_ok) Serial.printf("[probe]   ioexp in=0x%02X\n", s->ioexp_in);
    else Serial.println("[probe]   ioexp: no answer");
    if (s->pmu_ok) {
        Serial.printf(
            "[probe]   pmu irq 0x44=0x%02X 0x45=0x%02X 0x46=0x%02X 0x47=0x%02X\n",
            s->pmu_irq[0], s->pmu_irq[1], s->pmu_irq[2], s->pmu_irq[3]);
    } else {
        Serial.println("[probe]   pmu: no answer");
    }
    if (s->touch_ok) Serial.printf("[probe]   touch td=0x%02X\n", s->touch_td);
}

const char *pek_note(uint8_t reg, uint8_t value)
{
    if (reg != CATNIP_PMU_REG_IRQ_STATUS_3) return "";
    if (value & CATNIP_PMU_IRQ_PEK_SHORT) return " (power key short press)";
    if (value & CATNIP_PMU_IRQ_PEK_LONG) return " (power key long press)";
    return "";
}

/* Report the named switches as presses and releases rather than as pin
 * transitions, through the same filter the firmware itself uses. The raw
 * reading is the one taken under INPUT_PULLUP: these switches are active-low
 * against the board's own pull-up, so the pin falls to 0 while one is held.
 *
 * The loop samples every few milliseconds, which is several times inside the
 * debounce window, so the filter sees the level settle rather than a single
 * reading either side of it. */
void report_buttons(const Sample *cur, unsigned long now)
{
    for (size_t i = 0; i < PIN_COUNT; i++) {
        const char *name = button_name(ALLOWED_PINS[i]);

        catnip_debounce_update(&g_pin[i].filter, cur->pullup[i] == 0, (uint32_t)now);

        /* Every pin is counted; only the ones board.h can name announce
         * themselves as they happen. An unnamed pin still shows its raw
         * transitions below, and its count in the next tally. */
        if (catnip_debounce_take_pressed(&g_pin[i].filter)) {
            g_pin[i].press_count++;
            g_pin[i].down_at = now;
            g_pin[i].long_reported = false;
            if (name) Serial.printf("[press] %lu %s\n", now, name);
        }
        if (name && !g_pin[i].long_reported && catnip_debounce_down(&g_pin[i].filter) &&
            now - g_pin[i].down_at >= LONG_PRESS_MS) {
            Serial.printf("[longpress] %lu %s\n", now, name);
            g_pin[i].long_reported = true;
        }
        if (catnip_debounce_take_released(&g_pin[i].filter)) {
            g_pin[i].long_reported = false;
            if (name) Serial.printf("[release] %lu %s\n", now, name);
        }
    }
}

/* The authoritative record: how many debounced presses each pin has seen since
 * the probe started. Pins that have counted something get a line each; the
 * rest are named together on one line, because a pin sitting at zero is a
 * result - it is what "this switch does not reach this pin" looks like - and
 * a pin that is merely absent from the block says nothing at all. */
void print_tally(unsigned long now)
{
    bool any_zero = false;

    Serial.printf("[tally] %lu\n", now);
    for (size_t i = 0; i < PIN_COUNT; i++) {
        const char *name = button_name(ALLOWED_PINS[i]);
        if (g_pin[i].press_count == 0) continue;
        if (name) {
            Serial.printf("[tally]   %s (gpio%u) = %lu\n", name, ALLOWED_PINS[i],
                          g_pin[i].press_count);
        } else {
            Serial.printf("[tally]   gpio%u = %lu\n", ALLOWED_PINS[i],
                          g_pin[i].press_count);
        }
    }
    for (size_t i = 0; i < PIN_COUNT; i++) {
        const char *name = button_name(ALLOWED_PINS[i]);
        if (g_pin[i].press_count != 0) continue;
        if (!any_zero) {
            Serial.print("[tally]   still zero:");
            any_zero = true;
        }
        if (name) Serial.printf(" gpio%u(%s)", ALLOWED_PINS[i], name);
        else Serial.printf(" gpio%u", ALLOWED_PINS[i]);
    }
    if (any_zero) Serial.println();
}

/* Print what differs from the previous pass, one line per change, and leave
 * the PMIC's latched interrupts cleared so the next press is visible. */
void print_changes(const Sample *prev, const Sample *cur, unsigned long now)
{
    for (size_t i = 0; i < PIN_COUNT; i++) {
        uint8_t pin = ALLOWED_PINS[i];
        /* A pin board.h can name has already been reported by name, and
         * printing its transitions again would bury the pins that are still
         * unaccounted for - which are the ones being hunted. Every other pin
         * is logged exactly as it was on the first pass. */
        if (button_name(pin)) continue;
        if (prev->pullup[i] != cur->pullup[i]) {
            Serial.printf("[probe] %lu gpio%u pullup %u->%u\n", now, pin, prev->pullup[i],
                          cur->pullup[i]);
        }
        if (prev->pulldown[i] != cur->pulldown[i]) {
            Serial.printf("[probe] %lu gpio%u pulldown %u->%u\n", now, pin,
                          prev->pulldown[i], cur->pulldown[i]);
        }
        if (cur->adc[i] != ADC_NONE &&
            abs(prev->adc[i] - cur->adc[i]) >= ADC_CHANGE_THRESHOLD) {
            Serial.printf("[probe] %lu gpio%u adc %d->%d\n", now, pin, prev->adc[i],
                          cur->adc[i]);
        }
    }

    if (prev->ioexp_ok != cur->ioexp_ok) {
        Serial.printf("[probe] %lu ioexp %s\n", now,
                      cur->ioexp_ok ? "answered" : "no answer");
    }
    if (cur->ioexp_ok && prev->ioexp_in != cur->ioexp_in) {
        Serial.printf("[probe] %lu ioexp in 0x%02X->0x%02X", now, prev->ioexp_in,
                      cur->ioexp_in);
        for (uint8_t bit = 0; bit < 8; bit++) {
            uint8_t was = (prev->ioexp_in >> bit) & 1u, is = (cur->ioexp_in >> bit) & 1u;
            if (was != is) Serial.printf(" io%u:%u->%u", bit, was, is);
        }
        Serial.println();
    }

    if (prev->pmu_ok != cur->pmu_ok) {
        Serial.printf("[probe] %lu pmu %s\n", now,
                      cur->pmu_ok ? "answered" : "no answer");
    }
    for (uint8_t i = 0; cur->pmu_ok && i < PMU_IRQ_COUNT; i++) {
        uint8_t reg = (uint8_t)(PMU_IRQ_FIRST + i);
        if (prev->pmu_irq[i] == cur->pmu_irq[i]) continue;
        Serial.printf("[probe] %lu pmu irq 0x%02X 0x%02X->0x%02X%s\n", now, reg,
                      prev->pmu_irq[i], cur->pmu_irq[i], pek_note(reg, cur->pmu_irq[i]));
        /* Writing the bits back is what clears them, so the next pass reads
         * 0x00 and prints a second line going from the value back to zero.
         * That line is this write, not a second press: a press shows up in the
         * log as a pair. */
        if (cur->pmu_irq[i])
            catnip_i2c_write_reg(CATNIP_I2C_ADDR_PMU, reg, cur->pmu_irq[i]);
    }

    /* Only the first finger down and the last one up are reported. A drag
     * across the panel is not what is being established here; that the panel
     * answers, and with what numbers, is.
     *
     * The coordinates are the controller's own, deliberately not turned into
     * screen coordinates: the panel is a 240x320 part presented as a 320x240
     * screen, and working out that transform is Unit 2's job. It needs raw
     * numbers to work from, not numbers already run through a guess. */
    if (cur->touch_ok) {
        bool was_down = (prev->touch_td & 0x0F) != 0;
        bool is_down = (cur->touch_td & 0x0F) != 0;
        uint16_t x = 0, y = 0;

        if (!was_down && is_down) {
            if (!read_touch_point(&x, &y)) {
                Serial.printf(
                    "[touch] %lu down, but the coordinate registers did not answer\n",
                    now);
            } else if (x >= CATNIP_LCD_PANEL_W || y >= CATNIP_LCD_PANEL_H) {
                Serial.printf(
                    "[touch] %lu down, registers 0x03-0x06 read %u,%u - outside "
                    "the %ux%u panel, so this part is not holding coordinates "
                    "there\n",
                    now, x, y, CATNIP_LCD_PANEL_W, CATNIP_LCD_PANEL_H);
            } else {
                Serial.printf("[touch] %lu down (%u, %u)\n", now, x, y);
            }
        } else if (was_down && !is_down) {
            Serial.printf("[touch] %lu up\n", now);
        }
    }
}

} /* namespace */

void setup()
{
    /* Same first step as the real firmware, for the same reason: without the
     * latch the board switches itself off before anyone has pressed anything. */
    catnip_power_hold();

    Serial.begin(115200);
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 8000) {
        delay(10);
    }
    Serial.println("[probe] start");

    /* Three scans, not two. The expander sits on LDO4, which the PMIC bring-up
     * may be the thing that switches on, so a scan taken before it says what
     * the board looks like at reset, and the diff after it says which chips
     * the rails brought. The scan after the expander bring-up is the one the
     * touch controller is expected to appear in, if its reset is IO6. */
    catnip_i2c_begin();
    BusMap at_reset, after_pmu, after_ioexp;
    scan_bus(&at_reset);
    print_bus("at reset", &at_reset);

    if (!catnip_pmu_begin()) Serial.println("[probe] WARN: PMIC not found");
    scan_bus(&after_pmu);
    print_bus("after pmu", &after_pmu);
    print_bus_diff("pmu", &at_reset, &after_pmu);

    if (!catnip_ioexp_begin()) Serial.println("[probe] WARN: I/O expander not found");
    scan_bus(&after_ioexp);
    print_bus("after ioexp", &after_ioexp);
    print_bus_diff("ioexp", &after_pmu, &after_ioexp);

    g_touch_present = bus_has(&after_ioexp, TOUCH_ADDR);
    if (g_touch_present) print_touch_identity();
    else Serial.println("[probe] nothing at 0x38 after bring-up");

    /* A local: whether 0x68 answered the scan is asked once, here, and every
     * decision after this one keys off streaming or scale_known instead. */
    bool imu_answered = bus_has(&after_ioexp, IMU_ADDR);
    if (!imu_answered) {
        Serial.println("[imu] nothing at 0x68 after bring-up");
    } else {
        print_imu_identity();
        if (g_imu.streaming) imu_begin();
    }

    sample_pins(&g_prev);
    sample_i2c(&g_prev);
    g_last_tally = millis();
    for (size_t i = 0; i < PIN_COUNT; i++) {
        catnip_debounce_init(&g_pin[i].filter, (uint32_t)g_last_tally);
        g_pin[i].down_at = g_last_tally;
        g_pin[i].long_reported = false;
        g_pin[i].press_count = 0;
    }
    /* The one and only full snapshot: what every pin reads with nothing
     * pressed. From here on the tally carries that state, in a form that
     * survives the link dropping a block of it. */
    print_snapshot(&g_prev, g_last_tally);
    Serial.println("[probe] press ONE switch a known number of times, then read the");
    Serial.println("[probe] next [tally] block and see which pin counted that many.");
    Serial.println("[probe] The link drops lines: trust the totals, not the order.");
    print_tally(g_last_tally);
    if (g_imu.streaming) {
        g_last_imu = g_last_tally;
        Serial.println("[imu] The edge named on each line is a GUESS about how the part");
        Serial.println("[imu] is mounted, not a measurement. Turn the device so each");
        Serial.println("[imu] screen edge in turn points at the ceiling, hold it there,");
        Serial.println("[imu] and read the line. Correcting a wrong name is an edit to");
        Serial.println("[imu] IMU_UP_EDGE in probe_input.cpp and nothing else.");
    }
}

void loop()
{
    Sample cur;
    sample_pins(&cur);
    sample_i2c(&cur);

    unsigned long now = millis();
    report_buttons(&cur, now);
    print_changes(&g_prev, &cur, now);
    if (now - g_last_tally >= TALLY_MS) {
        print_tally(now);
        g_last_tally = now;
    }
    if (g_imu.streaming && now - g_last_imu >= IMU_MS) {
        print_imu(now);
        g_last_imu = now;
    }
    g_prev = cur;
}
