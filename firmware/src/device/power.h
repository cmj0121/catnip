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

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_POWER_H */
