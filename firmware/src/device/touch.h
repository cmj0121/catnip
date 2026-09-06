/*
 * touch.h - the capacitive touch panel: one finger, in screen coordinates.
 *
 * The controller is an FT6336 on the shared I2C bus at CATNIP_I2C_ADDR_TOUCH,
 * confirmed on the device by its identity registers rather than assumed - see
 * board.h. Unlike the switches in input.h, every read here is a bus
 * transaction shared with the PMIC, the I/O expander and whatever else is on
 * that bus, so polling is not free and the caller decides how often it happens.
 *
 * This driver is the whole of what the hardware offers: whether a finger is
 * down, where it is, and when it arrives and leaves. It reports the first
 * touch point only. The part can track two, and nothing in Catnip has yet
 * asked for a gesture that needs the second, so reading it would be a second
 * pair of registers on a shared bus in support of no caller.
 *
 * Positions come out in screen coordinates - the 320x240 the user is looking
 * at - and not in the controller's own 240x320 panel coordinates. Every caller
 * would otherwise have to know how the panel is mounted, and would sooner or
 * later get it wrong somewhere the tests cannot see it. The rotation itself
 * lives in touch_map.h, where a host test drives it and where the one
 * correction it may still need can be made in one place.
 *
 * Deciding what a tap means - focus, ui.fire, an app callback - belongs to the
 * layer above and is issue #31, exactly as it does for the buttons.
 */
#ifndef CATNIP_TOUCH_H
#define CATNIP_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Confirm the part and get ready to poll. The I2C bus must already be up
 * (catnip_i2c_begin()), because that is shared and not this driver's to own.
 *
 * Returns true only when an FT6336 answered: the identity registers are read
 * and checked, and anything else that happens to sit at that address is
 * reported and then left alone. That check is not ceremony. Every register
 * this driver touches afterwards is meaningful only for this part, and some
 * other chip answering the same address would return numbers that decode into
 * perfectly plausible finger positions - a failure that looks like a badly
 * calibrated panel rather than like the wrong chip. When this returns false,
 * every function below reports nothing for the rest of the run. */
bool catnip_touch_begin(void);

/* Sample the controller: one register read to see whether a finger is down,
 * and four more to find out where when one is. Call it from the main loop.
 *
 * There is no bounce filter here, and that is a difference from the buttons
 * rather than an omission. A mechanical contact chatters on both edges and had
 * to be filtered in software; the FT6336 does its own conditioning and reports
 * a finger count that the probe watched go up and down cleanly with the finger.
 * Filtering it again would only add latency to a bus that is already the
 * expensive part.
 *
 * A read that the controller does not acknowledge leaves the state exactly as
 * it was. A finger does not lift because the bus was busy, and inventing a
 * release from a failed transaction would hand the layer above an event the
 * user never made. */
void catnip_touch_poll(void);

/* True while a finger is on the glass, as of the last poll. */
bool catnip_touch_down(void);

/* True once when a finger arrives, and once when it leaves. The edge is
 * cleared as it is read, so it reaches exactly one caller - the same bargain
 * catnip_input_pressed() makes, and for the same reason: a caller that polls
 * less often than the driver still sees the tap instead of missing it between
 * two of its own calls. */
bool catnip_touch_tapped(void);
bool catnip_touch_lifted(void);

/* Where the finger is, in screen coordinates, as of the last poll that read a
 * position successfully. Returns false, and writes nothing, when no position
 * has ever been read - before the first touch, or when the part is absent.
 *
 * The position outlives the touch on purpose: after catnip_touch_lifted() the
 * last known position is still here, which is where a caller has to look to
 * find out where the tap that just ended actually was. */
bool catnip_touch_position(uint16_t *screen_x, uint16_t *screen_y);

/* The same touch as the controller reported it, before the rotation: a
 * position on the 240x320 panel in the panel's own orientation. Returns false,
 * and writes nothing, under the same conditions as catnip_touch_position().
 *
 * This exists for the diagnostic page and for nothing else, and no ordinary
 * caller should want it - the whole point of this driver reporting screen
 * coordinates is that callers do not have to know how the panel is mounted.
 * But the handedness of that rotation is not yet confirmed (see touch_map.c),
 * and a page showing only the mapped position can say that a tap landed in the
 * wrong place without saying whether the controller or the rotation put it
 * there. Showing both side by side is the difference between diagnosing that
 * from the screen and having to take a serial capture. */
bool catnip_touch_panel_position(uint16_t *panel_x, uint16_t *panel_y);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_TOUCH_H */
