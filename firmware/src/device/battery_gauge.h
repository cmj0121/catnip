/*
 * battery_gauge.h - a measured cell voltage turned into the percentage
 * device.battery() reports.
 *
 * Split out of pmu.cpp for the reason the rest of this directory splits things
 * out: it is arithmetic rather than I/O, and the one decision inside it cannot
 * be checked on a device without a bench supply and a discharged battery. The
 * decision is refusing to answer - see below - and a mechanism whose whole job
 * is to say "I do not know" is exactly the one that will be quietly deleted by
 * someone who has never seen it fire, unless a test holds it in place.
 */
#ifndef CATNIP_BATTERY_GAUGE_H
#define CATNIP_BATTERY_GAUGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Where a single-cell lithium battery is empty and full, and the band outside
 * which a reading is not a battery at all. Published rather than kept private
 * so the test names the same numbers the code does.
 *
 * A cell below 2.5 V has been cut off by its own protection circuit and this
 * board would not be running; above 4.5 V it would be venting. */
#define CATNIP_BATTERY_EMPTY_MV         3300u
#define CATNIP_BATTERY_FULL_MV          4200u
#define CATNIP_BATTERY_PLAUSIBLE_MIN_MV 2500u
#define CATNIP_BATTERY_PLAUSIBLE_MAX_MV 4500u

/* `mv` as a percentage of charge, 0-100 - or -1 when `mv` is not a voltage a
 * single-cell lithium battery can hold, which on this board means the register
 * it came from is not the one the datasheet claims.
 *
 * That -1 is the point of this function. The AXP173's battery ADC registers
 * have never been confirmed against a meter on this unit, and the vendor's
 * documentation for this board has already been wrong three times, so a decoded
 * number is only trustworthy if it lands somewhere a battery could be. Clamping
 * an implausible reading into 0-100 would turn a wrong register into a
 * confident wrong percentage, which nothing downstream could ever detect;
 * refusing to answer is recoverable, because -1 already means "unknown" all the
 * way out to Lua.
 *
 * Between empty and full the percentage is linear, which a discharge curve is
 * not: a cell sits near 3.7 V for most of its life and then falls off a cliff,
 * so this reads high through the middle of a charge and drops faster than the
 * owner expects at the end. It is an estimate from one voltage, not a fuel
 * gauge; a real one would need the coulomb counter and a calibration this board
 * has never been given. */
int catnip_battery_percent_from_mv(uint32_t mv);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_BATTERY_GAUGE_H */
