/*
 * power.h - hold the MeowKit's power rail on.
 *
 * The board latches its own supply through a GPIO. Until firmware asserts it,
 * the device is running on the momentary press of the power button; assert it
 * first, before anything slow, or the board switches off mid-boot.
 */
#ifndef CATNIP_POWER_H
#define CATNIP_POWER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Latch the power rail on. Safe to call more than once. */
void catnip_power_hold(void);

/* Release the latch: the board loses its own supply and switches off. Does not
 * return while the device is on battery. On USB the chip may keep running, so
 * callers must treat whatever follows as unreachable rather than as recovery. */
void catnip_power_off(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_POWER_H */
