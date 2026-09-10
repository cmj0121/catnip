/*
 * catnip_busy.h - the one thing this device does while it is working.
 *
 * A ring of dots with a bright one running round it. There is exactly one of
 * these and the platform draws it, for the same reason the platform draws the
 * action bar: "the device is busy" is a sentence a user has to be able to read
 * the same way everywhere, and an app that drew its own would eventually draw a
 * different one - a different place, a different rhythm, a different promise
 * about how long it means.
 *
 * It replaces three separate answers to the same question. The boot animation
 * was a four-frame paw cycle, the WiFi prober said "scanning" in a label it
 * refreshed itself, and an app being loaded said nothing at all - three shapes
 * for one state, and only one of them was a picture of waiting.
 *
 * The shape is here, in portable C with no panel in it, because it is drawn two
 * ways: straight onto the panel at boot, where LVGL is not up yet, and through
 * LVGL once it is. Two drawings of one arithmetic, rather than two animations
 * that have to be kept looking alike.
 */
#ifndef CATNIP_BUSY_H
#define CATNIP_BUSY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Eight, which is enough for the eye to read a direction and few enough that
 * one step is an obvious move rather than a shimmer. */
#define CATNIP_BUSY_DOTS 8

/* How long one dot holds the lead. Eight of these is one revolution, so the
 * ring turns once a second - the rate a person reads as "working" rather than
 * as "stuck" (too slow) or "panicking" (too fast). */
#define CATNIP_BUSY_STEP_MS 125u

/* One dot: where it goes, and how bright.
 *
 * `level` is 0 for the faintest and CATNIP_BUSY_DOTS-1 for the leader, so it is
 * a position in the tail rather than a colour - what a level looks like is the
 * drawing's business, and the two drawings do not have to agree about it beyond
 * "higher is brighter". */
typedef struct {
    int x, y;
    uint8_t level;
} catnip_busy_dot;

/* Which step of the cycle `now_ms` falls in. Taken from the clock rather than
 * counted, so the ring turns at the same rate whatever the loop is doing - a
 * spinner that sped up when the device had less work to do would be saying the
 * opposite of the truth. */
unsigned catnip_busy_phase(unsigned now_ms);

/* Where the eight dots are for `phase`, on a ring of `radius` about (cx, cy).
 * Writes at most `max` of them and returns how many. The first is at the top
 * and they run clockwise, which is the direction every other ring of dots in
 * the world turns. */
int catnip_busy_dots(unsigned phase, int cx, int cy, int radius, catnip_busy_dot *out,
                     int max);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_BUSY_H */
