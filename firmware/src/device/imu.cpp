/* imu.cpp - see imu.h. */
#include <Arduino.h>

#include "i2cbus.h"
#include "imu.h"
#include "imu_map.h"

/* Where the part is. board.h holds the addresses of the chips that have been
 * identified on this board, and 0x68 is still not one of them: board.h says
 * only that something answered there, and this driver has since established
 * that whatever answered is not what the vendor said. The address stays here,
 * and moves into board.h with a name beside it once the part has one.
 *
 * A QMI8658A puts its identity at register 0x00 and reports 0x05 there. This
 * unit reports 0x24, so every register named below - the whole QMI8658A map -
 * describes a part that is not on this board, and none of it is reached. It is
 * kept rather than deleted because the check that rejects it is what makes the
 * rejection legible, and because a second unit could yet answer 0x05. */
static const uint8_t kAddr = 0x68;
static const uint8_t kRegWhoAmI = 0x00;
static const uint8_t kRegCtrl1 = 0x02;
static const uint8_t kRegCtrl2 = 0x03;
static const uint8_t kRegCtrl7 = 0x08;
static const uint8_t kRegAccXL = 0x35;
static const uint8_t kWhoAmIQmi8658a = CATNIP_IMU_WHO_AM_I_QMI8658A;

/* The registers read when 0x68 turns out not to be a QMI8658A, and why each of
 * them tells the candidates apart.
 *
 * WHAT THIS IS FOR. On this device 0x00 reads 0x24, which is not the 0x05 a
 * QMI8658A reports, so hal_meowkit.cpp's comment naming that part is the third
 * thing the vendor's documentation has got wrong about this board after the
 * display's chip-select and all three button pins. This dump is how the part
 * gets identified before anybody writes a driver for it. There is a standing
 * hypothesis - a Bosch BMI270, whose CHIP_ID reads 0x24 and which sits at 0x68
 * with SDO low - and it is a hypothesis. The rule here is unchanged: a part is
 * only called something once its own registers say so, and printing values is
 * how that gets decided rather than by anyone's confidence.
 *
 * NOTHING IS WRITTEN. Every access below is a read, and the configuration
 * writes in catnip_imu_begin() happen only after 0x00 has matched, so an
 * unidentified part is never configured. That is not incidental tidiness: if
 * this is a BMI270 it needs an 8 KB configuration blob before it produces any
 * data at all, and poking a register map at it in the meantime is how the
 * display was left dark.
 *
 *   0x00  the identity that started this. Included so the dump stands alone
 *         and can be read without the line above it.
 *   0x01  ERR_REG on a BMI270, the revision on a QMI8658A. Its value is what
 *         separates the two families.
 *   0x02  ERR_REG or STATUS on some layouts, a data register on others.
 *   0x03  the same question one register along.
 *   0x21  INTERNAL_STATUS on a BMI270: 0x00 until its configuration blob has
 *         been uploaded, 0x01 afterwards. A part with nothing here is not one.
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

/* The bring-up, three writes.
 *
 * CTRL1 = 0x40 clears SensorDisable, which the part boots with set: in that
 * state the internal oscillator is off, the data registers never update, and
 * the chip answers every read with the same stale bytes. That looks exactly
 * like a device lying perfectly still, which is the one failure an orientation
 * driver cannot afford to be unable to see. Bit 6 alongside it turns on
 * address auto-increment, which is what lets the six acceleration bytes come
 * back in one transaction.
 *
 * CTRL2 = 0x16 is the accelerometer's own configuration: aFS = 001 for a range
 * of +-4g, and aODR = 0110 for 125 Hz. +-4g rather than +-2g because gravity
 * is 1g and a device being turned over by hand swings well past 2g on the way;
 * a clipped axis would read as the wrong attitude, at exactly the moment the
 * attitude is changing.
 *
 * CTRL7 = 0x01 enables the accelerometer and leaves the gyroscope off. */
static const uint8_t kCtrl1Value = 0x40;
static const uint8_t kCtrl2Value = 0x16;
static const uint8_t kCtrl7Value = 0x01;

/* CTRL2's aFS field, and the counts per g each of its four settings gives. The
 * scale is read back out of the chip rather than assumed from what was
 * written, so that a write which did not take is caught here instead of
 * silently scaling every reading afterwards by the wrong constant. */
static const uint8_t kCtrl2AfsShift = 4;
static const uint8_t kCtrl2AfsMask = 0x07;
static const int32_t kLsbPerG[4] = {16384, 8192, 4096, 2048};

/* How often the part is actually interrogated, and why this number.
 *
 * The budget being spent is the bus's, not this driver's. The QMI8658A shares
 * 400 kHz with the PMIC, the I/O expander, the RTC and the touch controller,
 * and each poll here is seven bytes on that bus. A caller polling from the
 * main loop would otherwise ask for an orientation as fast as the CPU can form
 * the question.
 *
 * Two bounds meet in the middle. The part is configured for 125 Hz, so
 * anything under 8 ms returns the same sample again and costs the bus for
 * nothing. At the other end, a device is turned over by hand in something like
 * half a second, and an arrow that lags that visibly would be read as a wrong
 * mapping rather than as a slow poll - which would defeat the page this driver
 * was written for. 50 ms is well inside the second bound (ten samples across a
 * turn, no perceptible lag) while costing the bus a twentieth of what an
 * ungated poll would.
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
static bool g_have_accel;
static int32_t g_mg[3];
static const catnip_imu_up *g_up;
static int32_t g_lsb_per_g;
static uint32_t g_last_poll_ms;

bool catnip_imu_begin(void)
{
    uint8_t ctrl2 = 0;
    uint8_t afs;

    g_present = false;
    g_have_who = false;
    g_have_accel = false;
    g_up = NULL;

    if (!catnip_i2c_read_reg(kAddr, kRegWhoAmI, &g_who)) {
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

    if (g_who != kWhoAmIQmi8658a) {
        /* Say what answered rather than carrying on. Configuration bytes
         * written into an unidentified chip are how this board's display was
         * left dark, and acceleration registers read out of one would produce
         * a steady, plausible, wrong orientation. */
        Serial.printf("[catnip] imu: 0x%02X reports 0x%02X, not the QMI8658A's 0x%02X - "
                      "not reading it as one\n",
                      kAddr, g_who, kWhoAmIQmi8658a);
        dump_identity();
        return false;
    }

    if (!catnip_i2c_write_reg(kAddr, kRegCtrl1, kCtrl1Value) ||
        !catnip_i2c_write_reg(kAddr, kRegCtrl2, kCtrl2Value) ||
        !catnip_i2c_write_reg(kAddr, kRegCtrl7, kCtrl7Value)) {
        Serial.println("[catnip] imu: the chip identified itself and then refused a "
                       "configuration write");
        return false;
    }

    if (!catnip_i2c_read_reg(kAddr, kRegCtrl2, &ctrl2)) {
        Serial.println("[catnip] imu: CTRL2 did not read back, so the range is unknown");
        return false;
    }

    afs = (uint8_t)((ctrl2 >> kCtrl2AfsShift) & kCtrl2AfsMask);
    if (afs > 3) {
        /* The range field came back as something this driver has no counts-per-g
         * for. Reporting acceleration anyway would mean scaling by a guess, and
         * a wrongly scaled axis crosses the dominance threshold at the wrong
         * tilt. The probe is the tool for looking at raw counts. */
        Serial.printf("[catnip] imu: CTRL2 read back 0x%02X, whose range field is not "
                      "one this driver knows\n",
                      ctrl2);
        return false;
    }
    g_lsb_per_g = kLsbPerG[afs];

    g_present = true;
    /* Backdated so the first poll after this reads the part rather than being
     * served from a sample that was never taken. */
    g_last_poll_ms = millis() - kPollIntervalMs;
    Serial.printf("[catnip] imu: QMI8658A at 0x%02X, range +-%dg at %ld counts per g\n",
                  kAddr, 2 << afs, (long)g_lsb_per_g);
    return true;
}

bool catnip_imu_who_am_i(uint8_t *out)
{
    if (!g_have_who) return false;

    *out = g_who;
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

    /* All six bytes in one transaction, which is what the address
     * auto-increment enabled in catnip_imu_begin() was for. It is also what
     * makes this one atomic sample: three separate reads could pair an X taken
     * before a turn with a Z taken after it, and the pair would decode to an
     * attitude the device was never in. */
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
