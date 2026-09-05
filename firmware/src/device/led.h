/*
 * led.h - the status LED, used as a sign of life.
 *
 * The display can fail, and USB CDC misses the first lines of the boot log
 * because the host has not attached yet. The LED answers the question those
 * cannot: is the firmware running at all?
 */
#ifndef CATNIP_LED_H
#define CATNIP_LED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void catnip_led_begin(void);

/* Change the breathing: `peak` is the brightest point of the cycle (0-255) and
 * `breaths_per_second` how often it repeats. Both come from the owner's config
 * file; calling this is optional and the built-in values are used until it is. */
void catnip_led_configure(uint8_t peak, float breaths_per_second);

/* Dim the breathing to a glimmer, or restore it. Used while the screen is off:
 * the light still has to say the device is alive, but a dark screen with a
 * bright LED next to it draws the eye to the one thing that is meant to be
 * ignored. */
void catnip_led_dim(bool dim);

/* 0 is off, 255 is full brightness. The part is an RGB LED; this scales the
 * brand colour rather than exposing three channels, because everything here
 * wants a status light, not a palette. */
void catnip_led_level(uint8_t brightness);

/* Drive one step of the breathing cycle. Call it often - it reads the clock
 * rather than blocking, so it costs nothing to call from a busy loop. */
void catnip_led_breathe(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_LED_H */
