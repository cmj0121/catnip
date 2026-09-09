/* rtc_time.c - see rtc_time.h. */
#include "rtc_time.h"

uint8_t catnip_from_bcd(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static uint8_t to_bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

/* A byte is only a number when both nibbles are digits. A register holding 0x1A
 * is not "26" and is not 0x1A either - it is a register that does not hold a
 * date, and saying so is the whole job of this check. */
bool catnip_bcd_valid(uint8_t v)
{
    return (v & 0x0F) <= 9 && (v >> 4) <= 9;
}

int32_t catnip_days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
    y -= m <= 2;
    const int32_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = (uint32_t)(y - era * 400);
    const uint32_t doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

void catnip_civil_from_days(int32_t z, int32_t *y, uint32_t *m, uint32_t *d)
{
    z += 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int32_t yr = (int32_t)yoe + era * 400;
    const uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const uint32_t mp = (5u * doy + 2u) / 153u;
    const uint32_t day = doy - (153u * mp + 2u) / 5u + 1u;
    const uint32_t mon = mp + (mp < 10u ? 3u : (uint32_t)-9);

    if (d) *d = day;
    if (m) *m = mon;
    if (y) *y = yr + (mon <= 2);
}

void catnip_rtc_split(uint32_t epoch, int32_t *y, uint32_t *mo, uint32_t *d, uint32_t *h,
                      uint32_t *mi, uint32_t *weekday)
{
    uint32_t days = epoch / 86400u;
    uint32_t secs = epoch % 86400u;

    catnip_civil_from_days((int32_t)days, y, mo, d);
    if (h) *h = secs / 3600u;
    if (mi) *mi = (secs % 3600u) / 60u;
    /* 1 January 1970 was a Thursday, and the index counts from Sunday. */
    if (weekday) *weekday = (days + 4u) % 7u;
}

uint32_t catnip_rtc_decode(const uint8_t regs[7])
{
    uint8_t sec, min, hour, day, month, year;

    if (!regs) return 0;
    /* The integrity flag. A clock that has lost its oscillator has no opinion
     * about the time, whatever its registers still hold. */
    if (regs[0] & 0x80) return 0;

    if (!catnip_bcd_valid((uint8_t)(regs[0] & 0x7F)) ||
        !catnip_bcd_valid((uint8_t)(regs[1] & 0x7F)) ||
        !catnip_bcd_valid((uint8_t)(regs[2] & 0x3F)) ||
        !catnip_bcd_valid((uint8_t)(regs[3] & 0x3F)) ||
        !catnip_bcd_valid((uint8_t)(regs[5] & 0x1F)) || !catnip_bcd_valid(regs[6]))
        return 0;

    sec = catnip_from_bcd((uint8_t)(regs[0] & 0x7F));
    min = catnip_from_bcd((uint8_t)(regs[1] & 0x7F));
    hour = catnip_from_bcd((uint8_t)(regs[2] & 0x3F));
    day = catnip_from_bcd((uint8_t)(regs[3] & 0x3F));
    /* regs[4] is the weekday, and nothing reads it: it is derived from the date
     * everywhere it is shown, and a weekday the chip lets software set on its
     * own is one more thing that can disagree with the date beside it. */
    month = catnip_from_bcd((uint8_t)(regs[5] & 0x1F));
    year = catnip_from_bcd(regs[6]);

    if (sec > 59 || min > 59 || hour > 23) return 0;
    if (day < 1 || day > 31 || month < 1 || month > 12) return 0;

    return (uint32_t)catnip_days_from_civil(2000 + year, month, day) * 86400u +
           hour * 3600u + min * 60u + sec;
}

bool catnip_rtc_encode(uint32_t epoch, uint8_t regs[7])
{
    int32_t y;
    uint32_t m, d;
    int32_t days;
    uint32_t secs;

    if (!regs) return false;
    days = (int32_t)(epoch / 86400u);
    secs = epoch % 86400u;
    catnip_civil_from_days(days, &y, &m, &d);
    if (y < 2000 || y > 2099) return false;

    /* Written with the top bit clear, which is what tells the chip its
     * timekeeping is trustworthy again. */
    regs[0] = to_bcd((uint8_t)(secs % 60u));
    regs[1] = to_bcd((uint8_t)((secs / 60u) % 60u));
    regs[2] = to_bcd((uint8_t)(secs / 3600u));
    regs[3] = to_bcd((uint8_t)d);
    /* Derived, so it cannot disagree with the date. 1970-01-01 was a Thursday,
     * and the register counts from Sunday. */
    regs[4] = (uint8_t)(((uint32_t)(days + 4)) % 7u);
    regs[5] = to_bcd((uint8_t)m);
    regs[6] = to_bcd((uint8_t)(y - 2000));
    return true;
}
