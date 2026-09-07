/*
 * touch_debug.h - the touch panel's raw reading, for the diagnostic page.
 *
 * touch.h's contract is that a caller gets screen coordinates and never has to
 * know how the panel is mounted. The function below breaks that on purpose,
 * and it is in a header of its own for exactly that reason: sitting in touch.h
 * beside catnip_touch_position() it read as the second of two equal options,
 * and the first caller in a hurry would have reached for whichever name looked
 * closer to what they had. Here it cannot be reached by accident. Writing
 * `#include "touch_debug.h"` is a sentence about what the including file is
 * doing, and if it ever appears outside a diagnostic that shows up in one
 * grep.
 *
 * The register knowledge stays in touch.cpp. What this exposes is a number
 * that has already been decoded, not the means of decoding one.
 */
#ifndef CATNIP_TOUCH_DEBUG_H
#define CATNIP_TOUCH_DEBUG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The last touch as the controller reported it: a position on the 240x320
 * panel, in the panel's own orientation, before the rotation in touch_map.c.
 * Returns false, and writes nothing, under the same conditions as
 * catnip_touch_position().
 *
 * It exists so that a diagnostic can show the raw pair beside the mapped one.
 * A page showing only the mapped position can say that a tap landed in the
 * wrong place without saying whether the controller or the rotation put it
 * there; the two side by side separate those on screen, with no serial
 * capture. That is how the handedness in touch_map.c was settled, and it is
 * why this survives now that it has been. */
bool catnip_touch_panel_position(uint16_t *panel_x, uint16_t *panel_y);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_TOUCH_DEBUG_H */
