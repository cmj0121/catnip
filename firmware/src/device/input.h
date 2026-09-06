/*
 * input.h - the seven physical switches: the joystick and buttons A and B.
 *
 * Each switch is a plain GPIO with an external pull-up, so reading them costs
 * nothing and touches no shared bus - see the measured map in board.h. This
 * driver is the whole of what the hardware offers: which switches are down,
 * and when one goes down or comes up. Deciding what a press means - focus,
 * ui.fire, an app callback - belongs to the layer above and is issue #31.
 */
#ifndef CATNIP_INPUT_H
#define CATNIP_INPUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The switches, in the order a diagnostic or a menu would want to walk them.
 * CATNIP_BTN_COUNT is the end of the array, not a switch. */
typedef enum {
    CATNIP_BTN_UP,
    CATNIP_BTN_DOWN,
    CATNIP_BTN_LEFT,
    CATNIP_BTN_RIGHT,
    CATNIP_BTN_CENTRE,
    CATNIP_BTN_A,
    CATNIP_BTN_B,
    CATNIP_BTN_COUNT
} catnip_button;

/* Set the pins up. Call once, before the first poll. */
void catnip_input_begin(void);

/* Sample every switch and update the filtered state. Call it from the main
 * loop as often as it comes round: it only reads pins and the clock, so
 * calling it often costs nothing. Calling it rarely costs presses, and not
 * only the ones shorter than the gap between two polls: a switch has to read
 * the same way across CATNIP_INPUT_DEBOUNCE_MS before the change is believed,
 * so the loop should come round several times inside that window. */
void catnip_input_poll(void);

/* True while the switch is held, as of the last poll. */
bool catnip_input_down(catnip_button button);

/* True once for each press, and once for each release. The edge is cleared as
 * it is read, so it is reported to exactly one caller; ask on every pass of
 * the loop, or hold what you were told. Having these here is the point of the
 * driver: a caller that only wants "did A just go down" should not have to
 * keep a copy of the last state and diff it. */
bool catnip_input_pressed(catnip_button button);
bool catnip_input_released(catnip_button button);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_INPUT_H */
