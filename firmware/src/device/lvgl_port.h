/*
 * lvgl_port.h - LVGL bound to this board's panel (#29).
 *
 * LVGL needs three things from a platform and this is all three: somewhere to
 * render, a clock, and a way to get the rendered pixels onto the glass. It
 * does not draw anything itself - it is what the diagnostic page (diag.h), the
 * ui.* renderer (#30) and the shell (#33) all draw through.
 *
 * IT GOES THROUGH THE PANEL DRIVER, NOT AROUND IT. The flush callback hands
 * its buffer to catnip_display_blit() and nothing here touches the SPI bus,
 * the backlight or the I/O expander. display.h's contract is a full-screen
 * blit, so LVGL is configured to render whole screens - see lvgl_port.cpp for
 * why that costs nothing over the alternative here - and the contract did not
 * have to widen for this.
 *
 * IT COMES UP WHEN ITS FIRST CLIENT ASKS, not at boot. An LVGL display with
 * nothing on it is a black screen, and bringing it up during setup() would put
 * that black screen in a fight with the boot animation, which goes straight
 * through catnip_display_blit() and knows nothing about LVGL. So
 * catnip_lvgl_begin() is called by whoever is about to draw, and until then
 * catnip_lvgl_step() is a no-op that the main loop can call unconditionally.
 */
#ifndef CATNIP_LVGL_PORT_H
#define CATNIP_LVGL_PORT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start LVGL and give it the panel. Safe to call more than once; the second
 * call reports the state of the first. Returns false when the draw buffer
 * could not be allocated, in which case nothing was started and the caller
 * should carry on without a screen rather than stop. */
bool catnip_lvgl_begin(void);

/* Let LVGL do its work: run its timers, redraw whatever was invalidated, and
 * flush it to the panel. Call it from the main loop, and from anywhere that
 * blocks for long enough that a still screen would be noticed. Does nothing
 * until catnip_lvgl_begin() has succeeded. */
void catnip_lvgl_step(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_LVGL_PORT_H */
