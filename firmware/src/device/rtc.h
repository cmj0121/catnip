/*
 * rtc.h - the real-time clock, if there is one (#70).
 *
 * `board.h` records 0x51 as "likely a PCF8563 / BM8563", and says in the same
 * breath why that is not good enough: nothing there had ever been identified by
 * reading a register, and this board's published documentation has already been
 * wrong about the display's chip-select, the display's reset, all three button
 * pins and the IMU. So this driver identifies the part before it believes
 * anything it says, the way imu.cpp does, and writes nothing until it has.
 *
 * Two families answer at 0x51 and they are not compatible - the same address,
 * the same purpose, and the seconds in a different register:
 *
 *   PCF8563 / BM8563   control at 0x00-0x01, time from 0x02
 *   PCF85063           control at 0x00-0x01, offset and RAM at 0x02-0x03,
 *                      time from 0x04
 *
 * Reading a PCF85063 as though it were a PCF8563 gets you its offset register
 * as the seconds and its scratch RAM as the minutes - numbers that look like a
 * time and are not one. That is the failure this file exists to avoid, and it
 * is why identification is a running clock ticking in the register it should
 * tick in rather than a name in a datasheet.
 */
#ifndef CATNIP_RTC_H
#define CATNIP_RTC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Identify whatever is at 0x51 and log the evidence. Returns true when the part
 * is one this driver can read.
 *
 * Nothing is written, here or anywhere else in this file except
 * catnip_rtc_set(). An unidentified part is left exactly as it was found. */
bool catnip_rtc_begin(void);

/* Seconds since 1970-01-01 UTC, or 0 for "this device does not know".
 *
 * Zero rather than a plausible-looking midnight, and it means all three of: no
 * clock chip, a clock whose oscillator has stopped, and a clock that has never
 * been set. Every one of those is the same fact to a caller - the time is not
 * known - and a clock face that showed 00:00 on the first of January would be
 * believed, which is worse than admitting it. The chips help here: both
 * families raise a bit when their timekeeping integrity is gone, and that bit
 * survives exactly as long as the fact does. */
uint32_t catnip_rtc_now(void);

/* Set the clock, in seconds since the epoch. Returns false when there is no
 * clock to set. The only function here that writes. */
bool catnip_rtc_set(uint32_t epoch);

/* What was found, for the device info page - "PCF8563", "PCF85063", or a
 * sentence saying nothing was identified. Never NULL. */
const char *catnip_rtc_part(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_RTC_H */
