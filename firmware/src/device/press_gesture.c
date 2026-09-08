/* press_gesture.c - see press_gesture.h. */
#include "press_gesture.h"

int catnip_press_step(catnip_press *p, bool down, unsigned now)
{
    if (!p) return CATNIP_PRESS_NONE;

    if (down && !p->down) { /* contact starts */
        p->down = true;
        p->fired = false;
        p->since = now;
        return CATNIP_PRESS_NONE;
    }

    if (down) { /* contact continues */
        /* Unsigned subtraction, so a millis() rollover reads as a small elapsed
         * time rather than an enormous one. The cost is a press spanning the
         * rollover measuring short; the alternative costs one measuring long
         * enough to fire home, which is the worse of the two by a lot. */
        if (!p->fired && (unsigned)(now - p->since) >= CATNIP_LONG_PRESS_MS) {
            p->fired = true;
            return CATNIP_PRESS_LONG;
        }
        return CATNIP_PRESS_NONE;
    }

    if (p->down) { /* contact ends */
        p->down = false;
        /* A release after the long already went out is the tail of that
         * gesture, not a press of its own. */
        if (p->fired) {
            p->fired = false;
            return CATNIP_PRESS_NONE;
        }
        return CATNIP_PRESS_SHORT;
    }

    return CATNIP_PRESS_NONE;
}
