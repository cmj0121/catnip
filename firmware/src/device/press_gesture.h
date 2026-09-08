/*
 * press_gesture.h - one switch, two meanings: short and long (#31).
 *
 * The interaction model is that a short press is an item's primary action and a
 * long press its secondary one, identically on A and B. That is two events out
 * of one contact, and which one it was is only known when the contact ends or
 * the threshold passes - so it is a small state machine over time rather than a
 * function of the current sample.
 *
 * It lives here, in plain C with no board and no LVGL, because the interesting
 * part is entirely about timing and the timing is exactly what cannot be
 * checked by pressing a button and watching a screen. ui_input.cpp owns one of
 * these per switch and feeds it what the debounced driver reports.
 *
 * The threshold is the probe's, deliberately: probe_input.cpp has been calling
 * a press long at 600 ms since the buttons were first mapped, and a device that
 * disagreed with its own instrument about what a long press is would make every
 * measurement taken with that instrument a lie.
 */
#ifndef CATNIP_PRESS_GESTURE_H
#define CATNIP_PRESS_GESTURE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CATNIP_LONG_PRESS_MS 600u

enum {
    CATNIP_PRESS_NONE = 0, /* nothing decided this sample */
    CATNIP_PRESS_SHORT,    /* released before the threshold */
    CATNIP_PRESS_LONG,     /* still held at the threshold */
};

typedef struct {
    bool down;      /* what the last sample said */
    bool fired;     /* the long already went out for this contact */
    unsigned since; /* when this contact started */
} catnip_press;

/*
 * Feed one sample and get at most one event back.
 *
 * The long fires *at the threshold*, while the finger is still down, rather
 * than on release. That is the difference between a gesture a user can learn
 * and one they cannot: holding B and seeing the launcher appear teaches that
 * the hold did something, where nothing-until-release teaches nothing and feels
 * broken. It costs the release its meaning, which is why `fired` exists - the
 * release that follows a long press emits nothing at all, or every long press
 * would also be a short one.
 *
 * `now` is a monotonic millisecond clock (millis()). A first sample that is
 * already down starts its contact now, so a switch held across a mode change
 * cannot fire a long press it did not earn in this mode.
 */
int catnip_press_step(catnip_press *p, bool down, unsigned now);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_PRESS_GESTURE_H */
