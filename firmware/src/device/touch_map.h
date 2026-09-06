/*
 * touch_map.h - the panel-to-screen rotation behind the touch driver.
 *
 * Kept apart from touch.cpp, and free of Arduino.h, for the same reason the
 * bounce filter is kept out of input.cpp: it is pure arithmetic over the
 * constants in board.h, so it can be checked against known corners on the host
 * instead of by touching a screen and squinting. That leaves touch.cpp as I2C
 * register reads and nothing else worth testing.
 *
 * It also gives the rotation exactly one home. The handedness below is not yet
 * confirmed (see touch_map.c), and when the corner capture settles it, the
 * correction is an edit to one function rather than a hunt for signs spread
 * through a driver.
 */
#ifndef CATNIP_TOUCH_MAP_H
#define CATNIP_TOUCH_MAP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Turn a coordinate as the touch controller reports it - a position on the
 * 240x320 panel, in the panel's own orientation - into the position on the
 * 320x240 screen the user is looking at.
 *
 * Returns false, and writes nothing, when the pair is not a point on the
 * panel. That is a real case rather than defensive tidying: the coordinate
 * registers are read one at a time over a shared bus, and the probe already
 * had to guard against a reading that landed outside the panel. A caller that
 * gets false should drop the sample, not draw at the edge.
 */
bool catnip_touch_panel_to_screen(uint16_t panel_x, uint16_t panel_y, uint16_t *screen_x,
                                  uint16_t *screen_y);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_TOUCH_MAP_H */
