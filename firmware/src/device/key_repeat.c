/* key_repeat.c - see key_repeat.h. */
#include "key_repeat.h"

bool catnip_repeat_step(catnip_repeat *r, bool down, unsigned now)
{
    if (!r) return false;

    if (!down) {
        r->down = false;
        r->repeating = false;
        return false;
    }

    if (!r->down) {
        /* The step happens on the edge, not after the delay: a press has to
         * answer at once or the key feels broken, and the delay is only there
         * to stop the second one arriving too soon. */
        r->down = true;
        r->repeating = false;
        r->since = now;
        return true;
    }

    /* Unsigned subtraction, so the millisecond counter wrapping once every
     * forty-nine days costs one step rather than jamming the key down. */
    unsigned held = now - r->since;
    if (!r->repeating) {
        if (held < CATNIP_REPEAT_DELAY_MS) return false;
        r->repeating = true;
        r->since = now;
        return true;
    }
    if (held < CATNIP_REPEAT_EVERY_MS) return false;
    r->since = now;
    return true;
}

bool catnip_repeat_first(const catnip_repeat *r)
{
    return r && r->down && !r->repeating;
}
