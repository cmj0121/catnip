/*
 * diag_layout.h - where the diagnostic page's boxes are, and which one a point
 * falls in.
 *
 * Kept apart from diag.cpp, and free of Arduino.h, for the same reason the
 * bounce filter is kept out of input.cpp and the rotation out of touch.cpp:
 * deciding whether a tap landed in the OK box is arithmetic over a fixed
 * table, so it can be checked on the host instead of by prodding a screen and
 * believing what happens. That leaves diag.cpp as drawing calls and driver
 * polls, which a host test could not judge anyway.
 *
 * The boxes are identified by catnip_button rather than by an enum of their
 * own. There is exactly one box per switch and they mean the same thing, so a
 * second enumeration would only be a table to keep in step - and the page's
 * whole job is to put a switch and its box side by side.
 */
#ifndef CATNIP_DIAG_LAYOUT_H
#define CATNIP_DIAG_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A box on the 320x240 screen. `x` and `y` are its top-left pixel and the box
 * covers the pixels up to and including x + w - 1 and y + h - 1. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} catnip_diag_rect;

/* The box that stands for `button`. Never null for a real button: the layout
 * is a compile-time table with one entry per switch, including the joystick
 * centre, which on this unit is the switch that never closes (see board.h).
 * Leaving it out of the layout would hide the very fault the page exists to
 * show. */
const catnip_diag_rect *catnip_diag_box(catnip_button button);

/* What is printed in the box: "UP", "OK", "A" and so on. */
const char *catnip_diag_label(catnip_button button);

/* The box containing the screen point, or -1 when the point is in none of
 * them - the gaps between the D-pad's arms, the text rows, or off the screen
 * entirely. The coordinates are signed and wider than a screen coordinate on
 * purpose, so that asking about a point outside the screen is an ordinary
 * question with the answer "no box" rather than something the caller has to
 * check for first.
 *
 * The return type is int, not catnip_button, because -1 is not a button. */
int catnip_diag_box_at(int32_t screen_x, int32_t screen_y);

/* Which quarter of the screen a point is in, in words: "top-left",
 * "top-right", "bottom-left" or "bottom-right".
 *
 * This exists because a picture turned out not to be enough. The page draws a
 * crosshair at the mapped touch position so that a wrong rotation shows up as
 * a marker away from the fingertip, and asked twice whether the marker
 * followed their finger, the owner answered about its colour both times. A
 * quadrant printed in words cannot be read past: "bottom-right" on the screen
 * while the finger is at the top left is the answer, stated, with nothing left
 * to interpret.
 *
 * A point off the screen is named by the same comparisons rather than
 * rejected, because a wrong rotation is exactly what could put the mapped
 * position off the edge, and a page that fell silent there would be silent in
 * the case it was built for. */
const char *catnip_diag_quadrant(int32_t screen_x, int32_t screen_y);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_DIAG_LAYOUT_H */
