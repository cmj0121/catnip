/* imu.cpp - see imu.h. */
#include <Arduino.h>

#include "bmi270_config.h"
#include "i2cbus.h"
#include "imu.h"
#include "imu_map.h"

/* Where the part is, and what it is. 0x68 is a Bosch BMI270: register 0x00
 * reads 0x24, which is a BMI270's CHIP_ID; 0x75 reads 0x00, which rules out
 * the MPU-6000/6050/6886 family that keeps its identity there; and STATUS
 * (0x03) = 0x10 beside INTERNAL_STATUS (0x21) = 0x00 is cmd_rdy with not_init,
 * the state of a BMI270 that has powered up and never been configured. Read
 * twice, a hundred milliseconds apart, with nothing moving between the passes
 * - which is exactly right, because a BMI270 in that state produces no data.
 *
 * The registers below are the BMI270's own map. Every one of them is reached
 * only after 0x00 has matched, so a part that is not this one is never written
 * to. board.h carries the same record beside the address. */
static const uint8_t kAddr = 0x68;
static const uint8_t kRegChipId = 0x00;
static const uint8_t kRegAccXL = 0x0C;
/* The gyroscope's six bytes sit immediately after the accelerometer's, which is
 * why there is no separate read for them: 0x0C..0x11 is acc X/Y/Z and
 * 0x12..0x17 is gyr X/Y/Z, so one twelve-byte burst is one atomic sample of
 * both. Two transactions could pair an acceleration from before a movement with
 * a rate from during it, and Air Mouse compares the two. */
static const uint8_t kRegInternalStatus = 0x21;
static const uint8_t kRegAccConf = 0x40;
static const uint8_t kRegAccRange = 0x41;
static const uint8_t kRegGyrConf = 0x42;
static const uint8_t kRegGyrRange = 0x43;
static const uint8_t kRegInitCtrl = 0x59;
static const uint8_t kRegInitAddr0 = 0x5B;
static const uint8_t kRegInitData = 0x5E;
static const uint8_t kRegPwrConf = 0x7C;
static const uint8_t kRegPwrCtrl = 0x7D;
static const uint8_t kChipIdBmi270 = CATNIP_IMU_CHIP_ID_BMI270;

/* The registers read when 0x68 does not report a BMI270's CHIP_ID, and why
 * each of them tells the candidate families apart.
 *
 * WHAT THIS IS FOR. It is what identified this part, and it stays because the
 * identity is a property of the unit in hand rather than of the design. The
 * vendor's documentation has now been wrong about this board's display
 * chip-select, its display reset, all three published button pins and the IMU;
 * a board that answers something other than 0x24 here deserves the same
 * treatment this one got rather than a driver written on faith.
 *
 * NOTHING IS WRITTEN. Every access below is a read, and the configuration
 * upload in catnip_imu_begin() happens only after 0x00 has matched, so an
 * unidentified part is never written to. That is not incidental tidiness:
 * pushing 8 KB of Bosch's firmware into a chip that is not a BMI270 is how the
 * display was left dark, one layer worse.
 *
 *   0x00  the identity. Included so the dump stands alone and can be read
 *         without the line above it.
 *   0x01  ERR_REG on a BMI270, the revision on a QMI8658A. Its value is what
 *         separates the two families.
 *   0x02  ERR_REG or STATUS on some layouts, a data register on others.
 *   0x03  the same question one register along. On a BMI270 this is STATUS,
 *         and 0x10 is cmd_rdy with no data ready.
 *   0x21  INTERNAL_STATUS on a BMI270: not_init (0x00) until its configuration
 *         image has been uploaded, init_ok (0x01) afterwards. A part with
 *         nothing here is not one.
 *   0x75  where the MPU-6000/6050/6886 family keeps its WHO_AM_I. Something
 *         sensible here means the part is from that family and 0x00 meant
 *         something else entirely.
 *
 * The dump is taken twice, a moment apart. 0x02 and 0x03 are data registers on
 * some of these layouts and status registers on others, and a value that
 * changes between the two passes says the part is streaming while a value that
 * does not says it is sitting still. Asking that question in one flash is the
 * point: this session has twice been bitten by conclusions that needed a
 * second run over a link that drops lines. */
static const uint8_t kIdentityRegs[] = {0x00, 0x01, 0x02, 0x03, 0x21, 0x75};

/* Long enough that a part streaming at any ordinary rate has produced new
 * samples, short enough to be unnoticeable in a boot. */
static const uint32_t kDumpGapMs = 100;

/* The upload, and the numbers it needs.
 *
 * PWR_CONF = 0x00 clears adv_power_save. The part boots with it set, and in
 * that state its internal clock is gated and it will not take the image at
 * all. It is left cleared afterwards too, for a different reason: with
 * advanced power save on, the data registers stop updating on their own, and
 * the part answers every read with the same stale bytes. That looks exactly
 * like a device lying perfectly still, which is the one failure an orientation
 * driver cannot afford to be unable to see.
 *
 * INIT_CTRL = 0x00 puts the part into configuration-load mode; INIT_CTRL =
 * 0x01 tells it the image is complete and to start executing it. Between those
 * two writes the part accepts nothing else.
 *
 * ACC_CONF = 0xA8 is acc_filter_perf = 1 (performance mode, so the output is
 * the filter's, not an average), acc_bwp = 0b010 (normal bandwidth) and
 * acc_odr = 0b1000 (100 Hz).
 *
 * ACC_RANGE = 0x01 is +-4g. +-4g rather than +-2g because gravity is 1g and a
 * device being turned over by hand swings well past 2g on the way; a clipped
 * axis would read as the wrong attitude, at exactly the moment the attitude is
 * changing.
 *
 * PWR_CTRL = 0x06 is acc_en and gyr_en together - bit 3 temp_en, bit 2 acc_en,
 * bit 1 gyr_en, bit 0 aux_en.
 *
 * The gyroscope used to be left off, and the reason was sound while it stood:
 * it cannot answer which way is down, and it draws several times the
 * accelerometer's current to not answer it. Air Mouse (#59) asks the other
 * question - not "which way is down" but "how fast is this being turned" - and
 * that is the one thing an accelerometer cannot answer at all. It cannot see
 * rotation about gravity (yaw) at any speed, so a pointing device built on it
 * can only ever be a tilt-to-scroll. So the part now runs both, and the current
 * is the price of the second question.
 *
 * GYR_CONF = 0xE8 is gyr_filter_perf = 1 (bit 7, the filter's output rather
 * than an average), gyr_noise_perf = 1 (bit 6, low-noise mode - this is a
 * pointing device, and the noise floor is a cursor that will not sit still),
 * gyr_bwp = 0b10 (normal bandwidth) and gyr_odr = 0b1000 (100 Hz, twice the
 * report rate above it so no report is built from a sample it has already sent).
 *
 * GYR_RANGE = 0x02 is +-500 dps, 65.6 LSB per dps. This was +-1000 on the
 * argument that a flick to reposition is several hundred degrees a second and a
 * range that clipped it would make the fastest gesture ambiguous. The vendor's
 * own air mouse for this board picks 500, and its reason is the better one: a
 * human hand swinging a device stays under 500 dps, so the wider range buys
 * nothing and throws away half the resolution - and a pointing device lives at
 * the *slow* end of its range, where resolution is the whole of whether a
 * cursor can be aimed at something small. */
static const uint8_t kPwrConfRun = 0x00;
static const uint8_t kInitCtrlLoad = 0x00;
static const uint8_t kInitCtrlRun = 0x01;
static const uint8_t kAccConfValue = 0xA8;
static const uint8_t kAccRangeValue = 0x01;
static const uint8_t kGyrConfValue = 0xE8;
static const uint8_t kGyrRangeValue = 0x02;
static const uint8_t kPwrCtrlAccGyr = 0x06;

/* Sensitivity for GYR_RANGE above, times ten, so the conversion stays integer:
 * 65.6 LSB per dps. Milli-dps out, because a pointing device cares about single
 * degrees a second and whole dps would quantise the slow end of the range into
 * steps a user can see. */
static const int32_t kGyrLsbPerDpsX10 = 656;

/* The wait after clearing adv_power_save, from Bosch's datasheet. The part
 * needs its clock up before it will accept the first byte of the image, and an
 * upload started too early fails silently - INTERNAL_STATUS simply never
 * reaches init_ok, with nothing to say why. */
static const uint32_t kPowerSettleUs = 450;

/* How much of the image goes out per transaction.
 *
 * Two bounds. The Wire library's buffer is 128 bytes and the register byte
 * takes one of them, so 127 is the ceiling. And the chunk must be even,
 * because INIT_ADDR counts halfwords (see below) and a chunk boundary has to
 * land on one. 64 divides the 8192-byte image exactly, into 128 transactions.
 *
 * Bosch's own API defaults to 128 here, which is one byte past what this
 * core's Wire can carry. Raising this to match it would truncate every
 * transaction. */
static const size_t kUploadChunk = 64;

/* How long to wait for init_ok. The datasheet puts the initialisation at under
 * 20 ms once INIT_CTRL goes high; this allows several times that before giving
 * up, because the cost of waiting too long is a slower boot and the cost of
 * giving up too early is a part declared broken that was merely slow. Polled
 * rather than slept through, so an ordinary boot pays about a millisecond. */
static const uint32_t kInitTimeoutMs = 150;
static const uint32_t kInitPollMs = 1;

/* ACC_RANGE's field, and the counts per g each of its four settings gives. The
 * scale is read back out of the chip rather than assumed from what was
 * written, so that a write which did not take is caught here instead of
 * silently scaling every reading afterwards by the wrong constant. */
static const uint8_t kAccRangeMask = 0x03;
static const int32_t kLsbPerG[4] = {16384, 8192, 4096, 2048};

/* How often the part is actually interrogated, and why this number.
 *
 * The budget being spent is the bus's, not this driver's. The BMI270 shares
 * 400 kHz with the PMIC, the I/O expander, the RTC and the touch controller,
 * and each poll here is seven bytes on that bus. A caller polling from the
 * main loop would otherwise ask for an orientation as fast as the CPU can form
 * the question.
 *
 * Two bounds meet in the middle. The part is configured for 100 Hz, so
 * anything under 10 ms returns the same sample again and costs the bus for
 * nothing. At the other end, a device is turned over by hand in something like
 * half a second, and an arrow that lags that visibly would be read as a wrong
 * mapping rather than as a slow poll - which would defeat the page this driver
 * was written for. 50 ms is well inside the second bound (ten samples across a
 * turn, no perceptible lag) while costing the bus a twentieth of what an
 * ungated poll would.
 *
 * The BMI270's 100 Hz moved the lower bound from the 8 ms the QMI8658A's 125 Hz
 * implied to 10 ms, and 50 ms clears it just as comfortably, so the number
 * stands rather than being changed for the sake of having rechecked it.
 *
 * The gate is here rather than in any caller so that it protects the bus from
 * all of them, including the ones not written yet - the same bargain
 * catnip_touch_poll() makes with its 12 ms. */
static const uint32_t kPollIntervalMs = 50;

/* One pass of the dump. A register that does not acknowledge is as informative
 * as one that returns a value - it says the part has no such register - so the
 * two are printed differently and must never be mistaken for each other. */
static void dump_identity_regs(int pass)
{
    for (size_t i = 0; i < sizeof(kIdentityRegs) / sizeof(kIdentityRegs[0]); i++) {
        uint8_t reg = kIdentityRegs[i];
        uint8_t value = 0;

        if (catnip_i2c_read_reg(kAddr, reg, &value)) {
            Serial.printf("[catnip] imu: pass %d reg 0x%02X = 0x%02X\n", pass, reg,
                          value);
        } else {
            Serial.printf("[catnip] imu: pass %d reg 0x%02X did not acknowledge\n", pass,
                          reg);
        }
    }
}

/* Both passes, with the standing question stated first so that the numbers
 * below arrive with something to be numbers about. */
static void dump_identity(void)
{
    Serial.printf("[catnip] imu: 0x%02X is not identified - dumping the registers that "
                  "tell the candidate families apart. Reads only; nothing is written to "
                  "this part.\n",
                  kAddr);
    dump_identity_regs(1);
    delay(kDumpGapMs);
    dump_identity_regs(2);
    Serial.println("[catnip] imu: a value that moved between pass 1 and pass 2 is a "
                   "register the part is updating; one that did not is not.");
}

static bool g_present;
static bool g_have_who;
static uint8_t g_who;
static bool g_have_status;
static uint8_t g_status;
static bool g_have_accel;
static int32_t g_mg[3];
/* The rate, in milli-degrees per second, and whether one has ever been read.
 * Separate flag from the accelerometer's: the two come out of one burst, so in
 * practice they arrive together, but a caller asking for a rate must not be
 * told "yes" on the strength of an acceleration. */
static bool g_have_gyro;
static int32_t g_mdps[3];
static const catnip_imu_up *g_up;
static int32_t g_lsb_per_g;
static uint32_t g_last_poll_ms;

/* Push Bosch's configuration image into the part.
 *
 * INIT_ADDR IS NOT A BYTE OFFSET. It is the position within the image counted
 * in 16-bit halfwords, and it is split unevenly across two registers:
 * INIT_ADDR_0 (0x5B) carries bits 3:0 of that halfword index and INIT_ADDR_1
 * (0x5C) carries bits 11:4. So the byte offset is halved before being sent,
 * and then chopped at four bits rather than eight.
 *
 * This is written out because it looks like a mistake and is not. Someone
 * reading `(offset / 2) & 0x0F` will see a byte offset being divided by two
 * for no reason and a mask that throws away most of it, and will be tempted to
 * "fix" it into `offset & 0xFF`. That fix builds, uploads without a single
 * failed transaction, and produces a part whose INTERNAL_STATUS never leaves
 * not_init - because every chunk after the first landed at the wrong place in
 * the image. It matches Bosch's own BMI270-Sensor-API, which does exactly this
 * in upload_file().
 *
 * The two address registers are consecutive, so they go out as one two-byte
 * write, which is also how Bosch's API does it.
 *
 * Returns false on the first transaction the part does not acknowledge. A
 * partial image is not a lesser success: the part would sit at not_init and
 * report nothing, and carrying on to write INIT_CTRL would ask it to execute
 * firmware with a hole in it. */
static bool upload_config_image(void)
{
    for (size_t offset = 0; offset < BMI270_CONFIG_SIZE; offset += kUploadChunk) {
        size_t halfword = offset / 2;
        uint8_t addr[2];

        addr[0] = (uint8_t)(halfword & 0x0F);
        addr[1] = (uint8_t)((halfword >> 4) & 0xFF);

        if (!catnip_i2c_write_regs(kAddr, kRegInitAddr0, addr, sizeof(addr))) {
            Serial.printf("[catnip] imu: INIT_ADDR refused at byte %u of %u\n",
                          (unsigned)offset, (unsigned)BMI270_CONFIG_SIZE);
            return false;
        }
        if (!catnip_i2c_write_regs(kAddr, kRegInitData, &bmi270_config_file[offset],
                                   kUploadChunk)) {
            Serial.printf("[catnip] imu: INIT_DATA refused at byte %u of %u\n",
                          (unsigned)offset, (unsigned)BMI270_CONFIG_SIZE);
            return false;
        }
    }
    return true;
}

/* Wait for the part to finish executing the image. Reports what it saw either
 * way, because "never reached init_ok" and "reported an error code" are
 * different faults and the raw byte is what tells them apart. */
static bool wait_for_init_ok(void)
{
    uint32_t started = millis();

    for (;;) {
        g_have_status = catnip_i2c_read_reg(kAddr, kRegInternalStatus, &g_status);
        if (g_have_status && (g_status & CATNIP_IMU_INIT_MSG_MASK) == CATNIP_IMU_INIT_OK)
            return true;

        /* Unsigned subtraction, so this stays correct across the wrap of
         * millis(), the same way the poll gate's clock does. */
        if (millis() - started >= kInitTimeoutMs) return false;
        delay(kInitPollMs);
    }
}

/* Whether the part is already running Bosch's image.
 *
 * WHAT THIS IS DECIDED FROM, and it is deliberately not a flag this driver
 * kept. Two things, both read out of the part on this call: its CHIP_ID
 * matched a BMI270's above, and INTERNAL_STATUS's message field reads init_ok
 * here. A BMI270 reports not_init until an image has been executed and init_ok
 * only afterwards, so that register is the part's own answer to the question -
 * which is the only answer worth having, because the alternatives are not
 * equivalent.
 *
 * A remembered "we already did this" would be wrong the moment the part lost
 * power without the MCU losing it. The sensors sit on the PMIC's LDO2 (see
 * board.h) and a BMI270 does not retain its image across a power cycle: it
 * comes back at not_init, silent, answering every read. Idempotent has to mean
 * "converge on a part that is up", not "only ever once per boot", and those
 * two differ exactly in the case that would be hardest to diagnose - a driver
 * reporting success over a part producing nothing.
 *
 * WHAT IT COSTS, which is the whole reason to ask rather than assume. This is
 * one register read. The upload it can avoid is 8192 bytes in 128
 * transactions, about a quarter of a second, over a 400 kHz bus that the PMIC,
 * the I/O expander, the RTC and the touch controller are also on. The check
 * has to be the cheap one of the two, and it is, by three orders of magnitude.
 *
 * A part that answers its identity but not this register is treated as not up.
 * That sends it down the upload path, where it fails loudly at
 * wait_for_init_ok() with the register named, rather than being quietly
 * declared ready on the strength of a read that never happened. */
static bool already_initialised(void)
{
    g_have_status = catnip_i2c_read_reg(kAddr, kRegInternalStatus, &g_status);
    return g_have_status && (g_status & CATNIP_IMU_INIT_MSG_MASK) == CATNIP_IMU_INIT_OK;
}

bool catnip_imu_begin(void)
{
    uint8_t range = 0;

    g_present = false;
    g_have_who = false;
    g_have_status = false;
    g_have_accel = false;
    g_have_gyro = false;
    g_up = NULL;

    if (!catnip_i2c_read_reg(kAddr, kRegChipId, &g_who)) {
        /* Either nothing is at the address or it did not answer a register
         * read. catnip_i2c_scan() is what separates the two, and it is worth
         * running when this line appears. */
        Serial.printf("[catnip] imu: 0x%02X did not answer its identity register\n",
                      kAddr);
        /* Dumped even here. If the address is dead every line below says so,
         * which is itself the finding; and a part that answers some registers
         * and not 0x00 is a different fault worth being able to see. */
        dump_identity();
        return false;
    }
    g_have_who = true;

    if (g_who != kChipIdBmi270) {
        /* Say what answered rather than carrying on. This is the check that
         * caught the part being mis-documented, and it does not get relaxed
         * now that the answer is known - 8 KB of Bosch's firmware pushed into
         * something that is not a BMI270 is a worse version of how this
         * board's display was left dark. */
        Serial.printf("[catnip] imu: 0x%02X reports 0x%02X, not a BMI270's 0x%02X - not "
                      "reading it as one\n",
                      kAddr, g_who, kChipIdBmi270);
        dump_identity();
        return false;
    }

    /* Cleared on both branches below, because adv_power_save has two separate
     * jobs to be out of the way of. The part will not take the image at all
     * with it set, and with it set the data registers stop updating on their
     * own and every read returns the same stale bytes - which looks exactly
     * like a device lying perfectly still. So a bring-up that skips the upload
     * still has to clear it, and this write stays ahead of the branch rather
     * than inside one. The datasheet's settling time is in microseconds rather
     * than milliseconds - delay(1) would do, and says less about why it is
     * there. */
    if (!catnip_i2c_write_reg(kAddr, kRegPwrConf, kPwrConfRun)) {
        Serial.println("[catnip] imu: PWR_CONF refused, so the part was left in "
                       "advanced power save and cannot be brought up");
        return false;
    }
    delayMicroseconds(kPowerSettleUs);

    /* The image goes out only when the part is not already running one. Two
     * callers now want the IMU - the HAL brings it up at boot and the
     * diagnostic page asks for it when it takes the screen - and before this
     * branch existed both of them paid the full upload, so 8 KB went over the
     * shared bus twice and the boot log printed the same success line for two
     * different things. Neither caller was wrong; a caller should be able to
     * ask for the IMU without knowing who asked first, and making that true is
     * this driver's job rather than theirs, because the driver is the only
     * thing that knows its own state. */
    if (already_initialised()) {
        Serial.printf("[catnip] imu: INTERNAL_STATUS = 0x%02X, already init_ok - the "
                      "image is still in the part from an earlier bring-up, so it is "
                      "not being sent again\n",
                      g_status);
    } else {
        if (!catnip_i2c_write_reg(kAddr, kRegInitCtrl, kInitCtrlLoad)) {
            Serial.println("[catnip] imu: INIT_CTRL would not go low, so the part never "
                           "entered configuration-load mode");
            return false;
        }

        Serial.printf("[catnip] imu: uploading %u bytes of configuration in %u-byte "
                      "chunks\n",
                      (unsigned)BMI270_CONFIG_SIZE, (unsigned)kUploadChunk);
        if (!upload_config_image()) return false;

        if (!catnip_i2c_write_reg(kAddr, kRegInitCtrl, kInitCtrlRun)) {
            Serial.println("[catnip] imu: the image went in but INIT_CTRL would not go "
                           "high, so the part was never told to run it");
            return false;
        }

        if (!wait_for_init_ok()) {
            /* The part is a BMI270 - 0x00 said so before a byte was written -
             * and it did not come up. That is its own outcome and is reported
             * as one: calling it "not a BMI270" here would contradict the
             * identity, and calling it a refused write would point at the bus
             * when every transaction was acknowledged. */
            if (g_have_status) {
                Serial.printf("[catnip] imu: INTERNAL_STATUS = 0x%02X after %lu ms, "
                              "never reached init_ok (0x%02X) - the part is a BMI270 "
                              "and did not come up\n",
                              g_status, (unsigned long)kInitTimeoutMs,
                              CATNIP_IMU_INIT_OK);
            } else {
                Serial.println("[catnip] imu: INTERNAL_STATUS stopped answering during "
                               "initialisation");
            }
            return false;
        }
        /* Said only on the branch that actually sent the image, because that
         * is the branch the claim is about: this part has just demonstrated it
         * is a BMI270 by executing Bosch's firmware, which is a stronger thing
         * than resembling one at register 0x00. A bring-up that skipped the
         * upload has not demonstrated it today and says something else above.
         * Two identical success lines for two different events is how the
         * duplicate upload stayed invisible until it reached a device. */
        Serial.printf("[catnip] imu: INTERNAL_STATUS = 0x%02X, init_ok - 0x%02X is a "
                      "BMI270, now confirmed by it accepting Bosch's image rather than "
                      "by its identity register alone\n",
                      g_status, kAddr);
    }

    /* Written on both branches, including the one that found the part already
     * up. Three register writes are cheaper than the three reads it would take
     * to check them and they leave a stronger postcondition: after this
     * returns true the accelerometer is configured the way this driver wants
     * it, rather than merely the way whoever got here first left it. That
     * matters because "already init_ok" says the image is running and says
     * nothing at all about ACC_CONF, ACC_RANGE or acc_en - a part could be
     * executing Bosch's firmware with its accelerometer switched off, and this
     * function would then have reported an accelerometer that produces
     * nothing. The expensive step is the 8 KB image, and that is the only one
     * the branch above skips. */
    if (!catnip_i2c_write_reg(kAddr, kRegAccConf, kAccConfValue) ||
        !catnip_i2c_write_reg(kAddr, kRegAccRange, kAccRangeValue) ||
        !catnip_i2c_write_reg(kAddr, kRegGyrConf, kGyrConfValue) ||
        !catnip_i2c_write_reg(kAddr, kRegGyrRange, kGyrRangeValue) ||
        !catnip_i2c_write_reg(kAddr, kRegPwrCtrl, kPwrCtrlAccGyr)) {
        Serial.println("[catnip] imu: the part initialised and then refused its "
                       "sensor configuration");
        return false;
    }

    if (!catnip_i2c_read_reg(kAddr, kRegAccRange, &range)) {
        Serial.println(
            "[catnip] imu: ACC_RANGE did not read back, so the range is unknown");
        return false;
    }
    g_lsb_per_g = kLsbPerG[range & kAccRangeMask];

    g_present = true;
    /* Backdated so the first poll after this reads the part rather than being
     * served from a sample that was never taken. */
    g_last_poll_ms = millis() - kPollIntervalMs;
    Serial.printf("[catnip] imu: accelerometer up at 100 Hz, range +-%dg at %ld counts "
                  "per g; gyroscope up at 100 Hz, range +-500 dps at %ld counts per "
                  "ten dps\n",
                  2 << (range & kAccRangeMask), (long)g_lsb_per_g,
                  (long)kGyrLsbPerDpsX10);
    return true;
}

bool catnip_imu_who_am_i(uint8_t *out)
{
    if (!g_have_who) return false;

    *out = g_who;
    return true;
}

bool catnip_imu_internal_status(uint8_t *out)
{
    if (!g_have_status) return false;

    *out = g_status;
    return true;
}

void catnip_imu_poll(void)
{
    uint8_t r[12];
    uint32_t now;
    int32_t mg[3];

    if (!g_present) return;

    /* Serve the last sample until the budget above has elapsed. Unsigned
     * subtraction, so this stays correct across the wrap of millis() every 49
     * days, the same way the bounce filter's clock does. */
    now = millis();
    if (now - g_last_poll_ms < kPollIntervalMs) return;
    g_last_poll_ms = now;

    /* All six bytes in one transaction. A BMI270 auto-increments its register
     * pointer across a burst read without being asked to, so unlike the part
     * this driver was first written for there is no bit to set for it. It is
     * what makes this one atomic sample: three separate reads could pair an X
     * taken before a turn with a Z taken after it, and the pair would decode to
     * an attitude the device was never in. */
    if (!catnip_i2c_read_regs(kAddr, kRegAccXL, r, sizeof(r))) return;

    for (uint8_t i = 0; i < 3; i++) {
        int16_t raw = (int16_t)((uint16_t)r[i * 2] | ((uint16_t)r[i * 2 + 1] << 8));

        mg[i] = ((int32_t)raw * 1000) / g_lsb_per_g;
        g_mg[i] = mg[i];
    }
    g_have_accel = true;
    g_up = catnip_imu_up_edge(mg);

    /* The second half of the same burst: the rate, from bytes six to eleven.
     * Scaled by ten thousand over the tenths-of-an-LSB constant, which is the
     * whole conversion in integers - raw / 32.8 dps, expressed as milli-dps
     * without ever leaving int32. The widest raw value is 32767, so the
     * numerator peaks around 3.3e8 and stays well inside the type. */
    for (uint8_t i = 0; i < 3; i++) {
        int16_t raw =
            (int16_t)((uint16_t)r[6 + i * 2] | ((uint16_t)r[6 + i * 2 + 1] << 8));

        g_mdps[i] = ((int32_t)raw * 10000) / kGyrLsbPerDpsX10;
    }
    g_have_gyro = true;
}

bool catnip_imu_acceleration(int32_t mg[3])
{
    if (!g_have_accel) return false;

    for (uint8_t i = 0; i < 3; i++)
        mg[i] = g_mg[i];
    return true;
}

bool catnip_imu_rotation(int32_t mdps[3])
{
    if (!g_have_gyro) return false;

    for (uint8_t i = 0; i < 3; i++)
        mdps[i] = g_mdps[i];
    return true;
}

const catnip_imu_up *catnip_imu_orientation(void)
{
    return g_up;
}
