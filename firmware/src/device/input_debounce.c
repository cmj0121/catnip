/* input_debounce.c - see input_debounce.h. */
#include "input_debounce.h"

#include "board.h"

void catnip_debounce_init(catnip_debounce *d, uint32_t now_ms)
{
    d->down = false;
    d->candidate = false;
    d->pressed = false;
    d->released = false;
    d->since_ms = now_ms;
}

void catnip_debounce_update(catnip_debounce *d, bool raw_down, uint32_t now_ms)
{
    if (raw_down != d->candidate) {
        /* The raw level moved. Start the count again; it has settled at
         * nothing yet. */
        d->candidate = raw_down;
        d->since_ms = now_ms;
        return;
    }
    if (raw_down == d->down) {
        return;
    }
    if ((uint32_t)(now_ms - d->since_ms) < CATNIP_INPUT_DEBOUNCE_MS) {
        return;
    }

    d->down = raw_down;
    if (raw_down) {
        d->pressed = true;
    } else {
        d->released = true;
    }
}

bool catnip_debounce_down(const catnip_debounce *d)
{
    return d->down;
}

bool catnip_debounce_take_pressed(catnip_debounce *d)
{
    bool seen = d->pressed;

    d->pressed = false;
    return seen;
}

bool catnip_debounce_take_released(catnip_debounce *d)
{
    bool seen = d->released;

    d->released = false;
    return seen;
}
