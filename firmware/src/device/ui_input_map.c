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

const char *catnip_ui_input_long_event(catnip_button button)
{
    switch (button) {
    /* The same pair as the short press, for the same reason: whatever a user
     * can activate, they can also ask about, and the centre may be dead. */
    case CATNIP_BTN_A:
    case CATNIP_BTN_CENTRE: return "options";
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

catnip_dir_meaning catnip_ui_input_dir(catnip_button button, catnip_node_layout layout,
                                       unsigned events, bool back, bool fwd)
{
    bool up = button == CATNIP_BTN_UP;
    bool down = button == CATNIP_BTN_DOWN;
    bool left = button == CATNIP_BTN_LEFT;
    bool right = button == CATNIP_BTN_RIGHT;

    if (!up && !down && !left && !right) return CATNIP_DIR_NOTHING;

    /* The home ring. Four directions, four different things, which is what
     * makes it the home section rather than a list laid out sideways: sideways
     * steps it, and up and down leave it for the planes either side.
     *
     * Up is NOTHING, and saying so is the point of this whole function. The
     * grid it will reach is #71 and is not built, so up on the cat currently
     * does nothing at all - which on a device with no hint is indistinguishable
     * from one that has stopped listening. Dimmed, it is a promise not yet
     * kept, which is a different thing and reads as one. */
    if (layout == CATNIP_LAYOUT_CAROUSEL) {
        if (left) return CATNIP_DIR_RING_BACK;
        if (right) return CATNIP_DIR_RING_FWD;
        if (down) return CATNIP_DIR_LEAVE_DOWN;
        return CATNIP_DIR_LEAVE_UP; /* up: the grid of every app (#71) */
    }

    /* A grid is a directory the ring walks in two dimensions. Both axes step
     * the selection - left and up back, right and down on - because the cells
     * flow left to right and wrap, so "back" and "on" are the two things a user
     * means however they push. It clamps, like the column it is a wrapped
     * version of; leaving it is B. */
    if (layout == CATNIP_LAYOUT_GRID) {
        if (left || up) return CATNIP_DIR_PREV;
        return CATNIP_DIR_NEXT;
    }

    /* A mixer is stepped sideways for the same reason a carousel is - its
     * children are laid out across the region - and the axis it does not use
     * for that is not spare: up and down are the value of the column under the
     * ring. So the two shapes share "sideways chooses" and part company on what
     * up and down mean. */
    if (layout == CATNIP_LAYOUT_MIXER) {
        if (left) return CATNIP_DIR_PREV;
        if (right) return CATNIP_DIR_NEXT;
        return up ? CATNIP_DIR_RAISE : CATNIP_DIR_LOWER;
    }

    /* Anywhere else: up and down are the focused node's selection, and they do
     * something exactly when it is listening for them - a page of facts is a
     * column that scrolls, a strip of two buttons is not. Sideways moves the
     * ring, which needs somewhere else to move it to. */
    if (up) return (events & CATNIP_EV_PREV) ? CATNIP_DIR_PREV : CATNIP_DIR_NOTHING;
    if (down) return (events & CATNIP_EV_NEXT) ? CATNIP_DIR_NEXT : CATNIP_DIR_NOTHING;
    if (left) return back ? CATNIP_DIR_FOCUS_BACK : CATNIP_DIR_NOTHING;
    return fwd ? CATNIP_DIR_FOCUS_FWD : CATNIP_DIR_NOTHING;
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
