/* rtc.cpp - see rtc.h. */
#include <Arduino.h>

#include "i2cbus.h"
#include "rtc.h"
#include "rtc_time.h"

namespace {

const uint8_t kAddr = 0x51;

/* Where each family keeps the first byte of the time, and what that byte is.
 *
 * On both, the low seven bits are the seconds in BCD and the top bit is the
 * integrity flag - VL on a PCF8563, OS on a PCF85063 - set by the chip when its
 * oscillator has stopped and cleared only by somebody writing a time. Same
 * meaning, same place within the byte, different register. */
const uint8_t kTimePcf8563 = 0x02;
const uint8_t kTimePcf85063 = 0x04;

enum rtc_kind {
    RTC_NONE = 0,
    RTC_PCF8563, /* and its BM8563 clone, which is register-identical */
    RTC_PCF85063,
};

rtc_kind g_kind = RTC_NONE;
uint8_t g_time_reg = 0;

/* Long enough that a running clock has certainly ticked, short enough not to be
 * felt in a boot. A second exactly would race the tick it is looking for. */
const uint32_t kTickGapMs = 1100;

/* A byte that could be a seconds register: binary-coded decimal, and in range.
 * The BCD half is rtc_time.c's, because that is the file that exists so no such
 * arithmetic lives beside the chip; only the 0-59 is about seconds. */
bool bcd_second(uint8_t v)
{
    return catnip_bcd_valid(v) && catnip_from_bcd(v) <= 59;
}

/* Does the byte at `reg` behave like seconds - a valid BCD 0-59 in both dumps,
 * and *one or two greater* in the second?
 *
 * One or two because the reads are kTickGapMs apart: a second always passes
 * between them and sometimes the boundary falls inside the gap.
 *
 * This asked only whether the byte had changed, once, and that was wrong for a
 * reason worth keeping written down. The argument for the loose test was that
 * the bus is shared and an exact increment would call a working clock broken -
 * but a shared bus costs milliseconds, not seconds, so it could never move a
 * seconds register by more than one tick. What the loose test actually let
 * through was a corrupt read: a boot where register 0x04 came back as 19 and
 * then 59 had two registers "ticking", so the part was refused and the clock
 * was dead for the rest of that session. A register that jumps forty seconds is
 * not a clock, and saying so is the whole job of this function. */
bool ticks_like_seconds(const uint8_t *a, const uint8_t *b, uint8_t reg)
{
    uint8_t s1 = (uint8_t)(a[reg] & 0x7F);
    uint8_t s2 = (uint8_t)(b[reg] & 0x7F);
    int step;

    if (!bcd_second(s1) || !bcd_second(s2)) return false;
    step = (catnip_from_bcd(s2) - catnip_from_bcd(s1) + 60) % 60;
    return step == 1 || step == 2;
}

} // namespace

bool catnip_rtc_begin(void)
{
    uint8_t a[0x10];
    uint8_t b[0x10];

    g_kind = RTC_NONE;

    if (!catnip_i2c_read_regs(kAddr, 0x00, a, sizeof(a))) {
        Serial.println("[catnip] rtc: nothing answers at 0x51");
        return false;
    }
    delay(kTickGapMs);
    if (!catnip_i2c_read_regs(kAddr, 0x00, b, sizeof(b))) {
        Serial.println("[catnip] rtc: 0x51 answered once and then stopped");
        return false;
    }

    /* The dump, both passes, before any conclusion is drawn from it. It stays
     * in the log for the reason imu.cpp's does: the identity is a property of
     * the unit in hand, and a board that answers something else deserves the
     * same treatment this one got rather than a driver written on faith. */
    Serial.print("[catnip] rtc: 0x51 dump");
    for (size_t i = 0; i < sizeof(a); i++)
        Serial.printf(" %02X", a[i]);
    Serial.println();
    Serial.print("[catnip] rtc: 0x51 again");
    for (size_t i = 0; i < sizeof(b); i++)
        Serial.printf(" %02X", b[i]);
    Serial.println();

    /* Which register is counting. This is the whole identification: the two
     * families put their seconds two registers apart, and what sits at the
     * other family's offset is a value that does not move - a trim offset on
     * one, a byte of scratch RAM on the other. */
    bool t8563 = ticks_like_seconds(a, b, kTimePcf8563);
    bool t85063 = ticks_like_seconds(a, b, kTimePcf85063);

    if (t8563 && !t85063) {
        g_kind = RTC_PCF8563;
        g_time_reg = kTimePcf8563;
    } else if (t85063 && !t8563) {
        g_kind = RTC_PCF85063;
        g_time_reg = kTimePcf85063;
    } else if (t8563 && t85063) {
        /* Both moved, which no single part can honestly do. Refusing is the
         * only safe answer: picking one would be a coin toss whose losing side
         * reads a trim offset as a minute for the life of the device. */
        Serial.println("[catnip] rtc: 0x51 has two registers ticking - not identified");
        return false;
    } else {
        /* Neither moved. Either the oscillator is stopped - which both families
         * say in the top bit of their seconds, and which is what an RTC that has
         * never been set looks like - or this is not an RTC at all. The two are
         * told apart by whether that flag is set on a byte that is otherwise a
         * valid BCD second. */
        bool stopped_8563 =
            (a[kTimePcf8563] & 0x80) && bcd_second((uint8_t)(a[kTimePcf8563] & 0x7F));
        bool stopped_85063 =
            (a[kTimePcf85063] & 0x80) && bcd_second((uint8_t)(a[kTimePcf85063] & 0x7F));
        if (stopped_8563 && !stopped_85063) {
            g_kind = RTC_PCF8563;
            g_time_reg = kTimePcf8563;
            Serial.println(
                "[catnip] rtc: PCF8563/BM8563, oscillator stopped - never set");
            return true;
        }
        if (stopped_85063 && !stopped_8563) {
            g_kind = RTC_PCF85063;
            g_time_reg = kTimePcf85063;
            Serial.println("[catnip] rtc: PCF85063, oscillator stopped - never set");
            return true;
        }
        Serial.println("[catnip] rtc: 0x51 does not keep time - not identified");
        return false;
    }

    Serial.printf("[catnip] rtc: %s, running\n", catnip_rtc_part());
    return true;
}

const char *catnip_rtc_part(void)
{
    switch (g_kind) {
    case RTC_PCF8563: return "PCF8563/BM8563";
    case RTC_PCF85063: return "PCF85063";
    default: return "none identified";
    }
}

uint32_t catnip_rtc_now(void)
{
    uint8_t t[7];

    if (g_kind == RTC_NONE) return 0;
    if (!catnip_i2c_read_regs(kAddr, g_time_reg, t, sizeof(t))) return 0;
    return catnip_rtc_decode(t);
}

bool catnip_rtc_set(uint32_t epoch)
{
    uint8_t t[7];

    if (g_kind == RTC_NONE) return false;
    if (!catnip_rtc_encode(epoch, t)) return false;
    return catnip_i2c_write_regs(kAddr, g_time_reg, t, sizeof(t));
}
