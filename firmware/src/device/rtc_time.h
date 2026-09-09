/*
 * rtc_time.h - the arithmetic behind the clock, lifted out of the driver (#70).
 *
 * What an RTC actually gives you is seven bytes of binary-coded decimal, and
 * what everything above wants is a count of seconds. The conversion between
 * them is where a clock goes wrong quietly: an off-by-one in a leap year, a
 * month read as a hex number, a two-digit year landing in the wrong century.
 * None of that needs a chip to reproduce, so none of it lives with the chip.
 *
 * rtc.cpp is then only the I2C: read seven bytes, hand them here.
 */
#ifndef CATNIP_RTC_TIME_H
#define CATNIP_RTC_TIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Days since 1970-01-01 for a civil date, and back. Howard Hinnant's
 * algorithms: exact for every date this device can hold, and with no table of
 * month lengths to get wrong. */
/* Binary-coded decimal, both ways, and whether a byte is one at all. Exported
 * because the driver has to ask the same question while it is identifying the
 * part - and the whole point of this file is that no BCD arithmetic lives with
 * the chip. */
uint8_t catnip_from_bcd(uint8_t v);
bool catnip_bcd_valid(uint8_t v);

int32_t catnip_days_from_civil(int32_t y, uint32_t m, uint32_t d);
void catnip_civil_from_days(int32_t z, int32_t *y, uint32_t *m, uint32_t *d);

/* Seven registers from a PCF8563 - seconds, minutes, hours, day, weekday,
 * century/month, year - into seconds since the epoch.
 *
 * Returns 0 for "the time is not known", which covers both of the ways that
 * happens: the chip's integrity flag is set (the top bit of the seconds, raised
 * when its oscillator has stopped and cleared only by a write), or the
 * registers do not spell a date at all. Both are reported the same way because
 * they are the same fact to a caller, and because the alternative - clamping
 * nonsense into a plausible date - is a confident wrong answer, which is the
 * one thing a clock must never give.
 *
 * The century bit beside the month is deliberately ignored and the year is read
 * as 2000-2099. This device will not be read in 1999, and the bit is one more
 * thing that differs between the parts that answer at this address. */
uint32_t catnip_rtc_decode(const uint8_t regs[7]);

/* And the other way, for setting it. Fills `regs` and returns false when the
 * epoch falls outside the years the two-digit register can hold. The seconds
 * are written with the integrity flag clear, which is the only way that bit is
 * ever cleared and therefore the only way a clock stops saying it is unset. */
bool catnip_rtc_encode(uint32_t epoch, uint8_t regs[7]);

/* An epoch, split into the pieces anything showing a clock needs: the date, the
 * time of day, and the weekday as an index from Sunday.
 *
 * Here rather than at each caller because there were three of them and they had
 * three copies of `(days + 4) % 7` between them - the magic that says 1 January
 * 1970 was a Thursday. Any pointer may be NULL. */
void catnip_rtc_split(uint32_t epoch, int32_t *y, uint32_t *mo, uint32_t *d, uint32_t *h,
                      uint32_t *mi, uint32_t *weekday);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_RTC_TIME_H */
