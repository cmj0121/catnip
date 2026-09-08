/* swipe.c - see swipe.h. */
#include "swipe.h"

#include <stdlib.h>

int catnip_swipe_step(catnip_swipe *s, bool down, int x, int y)
{
    if (!s) return CATNIP_SWIPE_NONE;

    if (!down) { /* the contact ended, or there never was one */
        s->down = false;
        s->moved = false;
        return CATNIP_SWIPE_NONE;
    }

    if (!s->down) { /* it starts here, and measures from here */
        s->down = true;
        s->moved = false;
        s->x = x;
        s->y = y;
        return CATNIP_SWIPE_NONE;
    }

    /* One step per contact. A finger that keeps travelling has not asked for
     * more of anything - it is one swipe, however far it goes - and a drag that
     * kept stepping made a carousel fly past whatever the user was looking at.
     * Wanting two steps means making two swipes, which is the same bargain the
     * joystick offers. */
    if (s->moved) return CATNIP_SWIPE_NONE;

    int dx = x - s->x;
    int dy = y - s->y;
    if (abs(dx) < CATNIP_SWIPE_STEP_PX && abs(dy) < CATNIP_SWIPE_STEP_PX)
        return CATNIP_SWIPE_NONE;

    s->moved = true;

    /* The larger travel decides. A finger never moves in a straight line, so
     * asking whether the other axis moved at all would make every vertical
     * drag ambiguous. */
    if (abs(dx) > abs(dy)) return (dx > 0) ? CATNIP_SWIPE_RIGHT : CATNIP_SWIPE_LEFT;
    return (dy > 0) ? CATNIP_SWIPE_DOWN : CATNIP_SWIPE_UP;
}

bool catnip_swipe_moved(const catnip_swipe *s)
{
    return s && s->moved;
}
