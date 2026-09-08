/*
 * swipe.h - a finger drag, read as one of the joystick's four directions (#31).
 *
 * A swipe is not a gesture of its own in this UI. It is the joystick, made with
 * a finger: the same four directions, meaning the same four things. Touch and
 * the joystick are two ways of saying one small vocabulary - a tap is already
 * short A on the row it landed on - so the directions follow the same rule
 * rather than growing a second grammar for fingers.
 *
 * Which direction a drag was is arithmetic on two points, so it lives in a
 * plain C file with no LVGL and no panel in it and the host build drives it.
 * LVGL has a gesture of its own and it is deliberately not used: it only fires
 * on an object nothing scrolled, it is not reachable from a host test, and the
 * threshold and the repeat below are decisions this UI wants to own.
 *
 * One drag is one step, however far the finger travels. A swipe is a request
 * to move by one, not a request to keep moving: a drag that kept stepping made
 * a carousel fly past whatever was being looked at, and asking for two means
 * making two swipes - the same bargain the joystick offers.
 */
#ifndef CATNIP_SWIPE_H
#define CATNIP_SWIPE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How far the finger travels per step. Small enough that a deliberate flick is
 * always at least one step, large enough that the jitter of a finger resting on
 * a capacitive panel is none: touch_map.c's own tests show a still finger
 * wandering a few pixels. */
#define CATNIP_SWIPE_STEP_PX 24

enum {
    CATNIP_SWIPE_NONE = 0,
    CATNIP_SWIPE_UP,
    CATNIP_SWIPE_DOWN,
    CATNIP_SWIPE_LEFT,
    CATNIP_SWIPE_RIGHT,
};

typedef struct {
    bool down;  /* what the last sample said */
    bool moved; /* this contact has produced at least one step */
    int x, y;   /* where the current step is measured from */
} catnip_swipe;

/*
 * Feed one sample of the panel and get at most one direction back.
 *
 * The axis with the larger travel wins, so a drag that is mostly vertical is
 * vertical even though a finger never moves in a straight line. Once a contact
 * has stepped it says nothing more until the finger lifts.
 */
int catnip_swipe_step(catnip_swipe *s, bool down, int x, int y);

/* Whether this contact has already been a swipe. The release that ends it is
 * then not a tap: LVGL sends a click on any press-and-release over an object
 * and does not care that the finger travelled, so without this one drag would
 * both move the selection and activate the row it started on - the same "two
 * meanings from one contact" that press_gesture.h exists to prevent. */
bool catnip_swipe_moved(const catnip_swipe *s);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_SWIPE_H */
