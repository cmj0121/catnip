/*
 * power.h - hold the MeowKit's power rail on.
 *
 * The board latches its own supply through a GPIO. Until firmware asserts it,
 * the device is running on the momentary press of the power button; assert it
 * first, before anything slow, or the board switches off mid-boot.
 */
#ifndef CATNIP_POWER_H
#define CATNIP_POWER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Latch the power rail on. Safe to call more than once. */
void catnip_power_hold(void);

/* True while the power button is held down. */
bool catnip_power_button_down(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_POWER_H */
