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
static const uint8_t kRegInternalStatus = 0x21;
static const uint8_t kRegAccConf = 0x40;
static const uint8_t kRegAccRange = 0x41;
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
 * PWR_CTRL = 0x04 is acc_en on its own - bit 3 temp_en, bit 2 acc_en, bit 1
 * gyr_en, bit 0 aux_en. The gyroscope stays off deliberately: it cannot answer
 * which way is down, and it draws several times the accelerometer's current to
 * not answer it. */
static const uint8_t kPwrConfRun = 0x00;
static const uint8_t kInitCtrlLoad = 0x00;
static const uint8_t kInitCtrlRun = 0x01;
static const uint8_t kAccConfValue = 0xA8;
static const uint8_t kAccRangeValue = 0x01;
static const uint8_t kPwrCtrlAccOnly = 0x04;

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

bool catnip_imu_begin(void)
{
    uint8_t range = 0;

    g_present = false;
    g_have_who = false;
    g_have_status = false;
    g_have_accel = false;
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

    /* The clock has to be running before the part will take the image, and the
     * datasheet's settling time is in microseconds rather than milliseconds -
     * delay(1) would do, and says less about why it is there. */
    if (!catnip_i2c_write_reg(kAddr, kRegPwrConf, kPwrConfRun)) {
        Serial.println("[catnip] imu: PWR_CONF refused, so the upload was not started");
        return false;
    }
    delayMicroseconds(kPowerSettleUs);

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
        /* The part is a BMI270 - 0x00 said so before a byte was written - and
         * it did not come up. That is its own outcome and is reported as one:
         * calling it "not a BMI270" here would contradict the identity, and
         * calling it a refused write would point at the bus when every
         * transaction was acknowledged. */
        if (g_have_status) {
            Serial.printf("[catnip] imu: INTERNAL_STATUS = 0x%02X after %lu ms, never "
                          "reached init_ok (0x%02X) - the part is a BMI270 and did not "
                          "come up\n",
                          g_status, (unsigned long)kInitTimeoutMs, CATNIP_IMU_INIT_OK);
        } else {
            Serial.println("[catnip] imu: INTERNAL_STATUS stopped answering during "
                           "initialisation");
        }
        return false;
    }
    Serial.printf("[catnip] imu: INTERNAL_STATUS = 0x%02X, init_ok - 0x%02X is a "
                  "BMI270, now confirmed by it accepting Bosch's image rather than by "
                  "its identity register alone\n",
                  g_status, kAddr);

    if (!catnip_i2c_write_reg(kAddr, kRegAccConf, kAccConfValue) ||
        !catnip_i2c_write_reg(kAddr, kRegAccRange, kAccRangeValue) ||
        !catnip_i2c_write_reg(kAddr, kRegPwrCtrl, kPwrCtrlAccOnly)) {
        Serial.println("[catnip] imu: the part initialised and then refused its "
                       "accelerometer configuration");
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
                  "per g, gyroscope left off\n",
                  2 << (range & kAccRangeMask), (long)g_lsb_per_g);
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
    uint8_t r[6];
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
}

bool catnip_imu_acceleration(int32_t mg[3])
{
    if (!g_have_accel) return false;

    for (uint8_t i = 0; i < 3; i++)
        mg[i] = g_mg[i];
    return true;
}

const catnip_imu_up *catnip_imu_orientation(void)
{
    return g_up;
}
