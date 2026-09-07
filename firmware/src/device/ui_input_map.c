/* ui_input_map.c - see ui_input_map.h. */
#include "ui_input_map.h"

const char *catnip_ui_input_event(catnip_button button)
{
    switch (button) {
    case CATNIP_BTN_UP: return "prev";
    case CATNIP_BTN_DOWN: return "next";
    /* A is the activation that always works; the centre is the one #49 doubts,
     * so both post the same event and neither is the sole way in. */
    case CATNIP_BTN_A:
    case CATNIP_BTN_CENTRE: return "click";
    default: return (const char *)0;
    }
}

int catnip_ui_input_focus_dir(catnip_button button)
{
    switch (button) {
    case CATNIP_BTN_LEFT: return -1;
    case CATNIP_BTN_RIGHT: return 1;
    default: return 0;
    }
}

catnip_handle catnip_ui_focus_step(const catnip_handle *order, int n,
                                   catnip_handle current, int dir)
{
    int i, at = -1;

    if (n <= 0) return CATNIP_HANDLE_NONE;

    for (i = 0; i < n; i++) {
        if (order[i] == current) {
            at = i;
            break;
        }
    }
    /* Not in the order: focus has nowhere to move from, so it starts over at the
     * top rather than trying to step relative to a node that is gone. */
    if (at < 0) return order[0];

    at += dir;
    if (at < 0) at = 0;
    if (at >= n) at = n - 1;
    return order[at];
}
