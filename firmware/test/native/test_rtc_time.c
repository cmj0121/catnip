/*
 * Native test for issue #70: the arithmetic behind the clock.
 *
 * The driver is I2C and needs the part; this is everything that can be wrong
 * without one - a leap year counted short, a month read as hex, a two-digit
 * year landing in the wrong century, and the two ways a clock says it does not
 * know the time.
 *
 * The last of those is the one worth the file on its own. A clock that answers
 * a confident 00:00 on the first of January will be believed, so "I do not
 * know" has to survive every path out of these registers: the integrity flag,
 * and bytes that do not spell a date.
 */
#include <stdio.h>
#include <string.h>

#include "device/rtc_time.h"

static int failures;
#define CHECK(cond, name)                                                                \
    do {                                                                                 \
        if (cond) {                                                                      \
            printf("  ok   - %s\n", name);                                               \
        } else {                                                                         \
            printf("  FAIL - %s\n", name);                                               \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

/* The registers a PCF8563 would hold for a given time, so a case reads as the
 * date it is about rather than as seven hex numbers. */
static void regs(uint8_t *r, int sec, int min, int hour, int day, int mon, int yr)
{
#define BCD(v) (uint8_t)((((v) / 10) << 4) | ((v) % 10))
    r[0] = BCD(sec);
    r[1] = BCD(min);
    r[2] = BCD(hour);
    r[3] = BCD(day);
    r[4] = 0; /* the weekday, which nothing reads */
    r[5] = BCD(mon);
    r[6] = BCD(yr);
#undef BCD
}

int main(void)
{
    uint8_t r[7];
    uint8_t out[7];
    int32_t y;
    uint32_t m, d;

    printf("the clock's arithmetic\n");

    /* The epoch itself, both directions. */
    CHECK(catnip_days_from_civil(1970, 1, 1) == 0, "1970-01-01 is day zero");
    catnip_civil_from_days(0, &y, &m, &d);
    CHECK(y == 1970 && m == 1 && d == 1, "and day zero is 1970-01-01");

    /* A leap year, on the day the naive version gets wrong. */
    CHECK(catnip_days_from_civil(2024, 3, 1) - catnip_days_from_civil(2024, 2, 28) == 2,
          "2024 has a 29th of February");
    CHECK(catnip_days_from_civil(2100, 3, 1) - catnip_days_from_civil(2100, 2, 28) == 1,
          "2100 does not, because a century is only a leap year every four");
    CHECK(catnip_days_from_civil(2000, 3, 1) - catnip_days_from_civil(2000, 2, 28) == 2,
          "and 2000 does, because four hundred is one of them");

    /* Round-trip every day for eight years, which covers two leaps and the
     * century's own rule at both ends of the range the chip can hold. */
    {
        int bad = 0;
        for (int32_t z = catnip_days_from_civil(2020, 1, 1);
             z <= catnip_days_from_civil(2028, 12, 31); z++) {
            catnip_civil_from_days(z, &y, &m, &d);
            if (catnip_days_from_civil(y, m, d) != z) bad++;
        }
        CHECK(bad == 0, "every day of 2020-2028 survives the round trip");
    }

    /* What the device was actually holding when its RTC was first identified:
     * 2026-03-15 19:33:14. Kept because it is a real reading rather than one
     * chosen to pass. */
    regs(r, 14, 33, 19, 15, 3, 26);
    {
        uint32_t t = catnip_rtc_decode(r);
        uint32_t days = t / 86400u;
        catnip_civil_from_days((int32_t)days, &y, &m, &d);
        CHECK(y == 2026 && m == 3 && d == 15, "the date the part was found holding");
        CHECK(t % 86400u == 19u * 3600u + 33u * 60u + 14u, "and the time with it");
    }

    /* The integrity flag: the chip saying its oscillator stopped. Everything
     * else in the registers still looks like a date, and it is still not one. */
    regs(r, 14, 33, 19, 15, 3, 26);
    r[0] |= 0x80;
    CHECK(catnip_rtc_decode(r) == 0, "a stopped oscillator is not a time");

    /* Registers that do not spell a date. Rejected rather than clamped: a
     * clamped nonsense date is a confident wrong answer. */
    regs(r, 14, 33, 19, 15, 3, 26);
    r[5] = 0x13; /* month 13 */
    CHECK(catnip_rtc_decode(r) == 0, "a thirteenth month is not a date");
    regs(r, 14, 33, 19, 15, 3, 26);
    r[3] = 0x00; /* day zero */
    CHECK(catnip_rtc_decode(r) == 0, "nor is a zeroth day");
    regs(r, 14, 33, 19, 15, 3, 26);
    r[1] = 0x6A; /* not BCD at all */
    CHECK(catnip_rtc_decode(r) == 0, "nor are minutes that are not digits");
    regs(r, 14, 33, 19, 15, 3, 26);
    r[2] = 0x25; /* hour 25 */
    CHECK(catnip_rtc_decode(r) == 0, "nor a twenty-fifth hour");

    /* The bits above the fields are the chip's, not ours: a set century bit
     * beside the month must not become part of the month. */
    regs(r, 14, 33, 19, 15, 3, 26);
    r[5] |= 0x80;
    CHECK(catnip_rtc_decode(r) != 0, "the century bit does not spoil the month");

    /* Writing, and reading back what was written. */
    {
        uint32_t t = (uint32_t)catnip_days_from_civil(2026, 9, 9) * 86400u + 10u * 3600u +
                     45u * 60u + 30u;
        CHECK(catnip_rtc_encode(t, out), "a time in range can be written");
        CHECK((out[0] & 0x80) == 0, "and is written with the integrity flag clear");
        CHECK(catnip_rtc_decode(out) == t, "and reads back as the same instant");
        /* 2026-09-09 was a Wednesday: the register counts from Sunday, so 3. */
        CHECK(out[4] == 3, "with a weekday derived from the date rather than given");
    }

    /* Outside what a two-digit year can hold, in both directions. */
    CHECK(!catnip_rtc_encode(0, out), "1970 is before this chip's range");
    CHECK(!catnip_rtc_encode((uint32_t)catnip_days_from_civil(2100, 1, 1) * 86400u, out),
          "and 2100 is past the end of it");

    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
