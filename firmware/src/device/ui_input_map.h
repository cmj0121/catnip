/*
 * ui_input_map.h - the switch-to-event convention, lifted out of the driver
 * (#31).
 *
 * The device turns a physical switch into either an event posted to the focused
 * node or a move of the focus cursor. Which switch means what, and what the
 * events are called, is a convention apps are written against - the File
 * Browser listens for `on_prev` / `on_next` / `on_click` - so it is decided
 * here, in plain C with no LVGL and no board, where a host test can hold the
 * convention still. ui_input.cpp is then the wiring that reads the switches and
 * the touch panel and calls catnip_render_post() with what these functions say.
 *
 * The convention as delivered: the joystick's UP/DOWN move a list's selection
 * (`prev` / `next`), LEFT/RIGHT move the focus between the focusable widgets, A
 * and the joystick centre activate (`click`), and B goes back. A activates as
 * well as the centre on purpose: board.h puts the centre on GPIO5 and issue #49
 * suspects GPIO5 is really IR_RX and the centre does not work on this unit, so
 * nothing may depend on the centre alone.
 *
 * A held switch means something else, and the two tables here are that: short A
 * activates and long A asks for the item's options. B is in neither, because
 * back and home are not delivered to a node at all - there is no node for
 * "leave" - so they leave through catnip_ui_input_step()'s return instead.
 */
#ifndef CATNIP_UI_INPUT_MAP_H
#define CATNIP_UI_INPUT_MAP_H

#include <stdbool.h>

#include "../catnip_render.h"
#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The render event a switch posts to the focused node, or NULL when the switch
 * moves the focus or goes back instead of posting. UP -> "prev", DOWN ->
 * "next", A and CENTRE -> "click"; everything else -> NULL. The names are the
 * ones ui.fire turns into on_prev / on_next / on_click, which is the contract
 * the app is written against. */
const char *catnip_ui_input_event(catnip_button button);

/* The render event a *held* switch posts, or NULL when holding it means nothing
 * to a node. A and CENTRE -> "options", which the focused list receives as
 * on_options(self, index) with the row that was selected; everything else ->
 * NULL, B included, because holding B is home and home never reaches an app. */
const char *catnip_ui_input_long_event(catnip_button button);

/* How a switch moves the focus cursor: -1 for LEFT (the previous focusable),
 * +1 for RIGHT (the next), 0 for a switch that does not move focus. */
int catnip_ui_input_focus_dir(catnip_button button);

/* What one of the four directions means where the focus currently is.
 *
 * The four do not mean the same thing everywhere, and that is deliberate - a
 * carousel is stepped sideways, a mixer's columns are chosen sideways and its
 * value changed up and down, a column is scrolled up and down and its
 * neighbours reached sideways. Which of those applies is decided by the shape
 * on screen, never by a mode.
 *
 * Both callers of that decision are here rather than each making it: the input
 * step, which acts on it, and the control hint, which draws a direction lit
 * when it is anything but NOTHING and dimmed when it is not (#80). Two copies
 * of this would drift the first time a layout gained a meaning, and the way
 * they would drift is a hint that lies - which is worse than no hint, because
 * the whole value of the hint is in the dimming. */
typedef enum {
    CATNIP_DIR_NOTHING = 0, /* dimmed: this direction does nothing here */
    CATNIP_DIR_PREV,        /* post "prev" to the focused node */
    CATNIP_DIR_NEXT,
    CATNIP_DIR_RAISE, /* a mixer column's value, up and down */
    CATNIP_DIR_LOWER,
    CATNIP_DIR_FOCUS_BACK, /* move the ring to the previous focusable */
    CATNIP_DIR_FOCUS_FWD,
    CATNIP_DIR_RING_BACK, /* step the home carousel */
    CATNIP_DIR_RING_FWD,
    CATNIP_DIR_LEAVE_UP,   /* off the ring: the grid of every app (#71) */
    CATNIP_DIR_LEAVE_DOWN, /* off the ring: what this device is */
    CATNIP_DIR_PAGE_BACK,  /* a paged shape: the screenful before this one */
    CATNIP_DIR_PAGE_FWD,
} catnip_dir_meaning;

/* Where the ring is, as everything that changes what a direction means there.
 *
 * A struct rather than a longer argument list because the list had grown to the
 * point where a caller could swap two bools and still compile. It is still a
 * plain statement of a situation with no tree in it, which is what lets a host
 * test hold the convention still by writing one down. */
typedef struct {
    catnip_node_layout layout; /* the focused node's */
    unsigned events;           /* its CATNIP_EV_* bits */
    /* Whether the focus ring has anywhere to go each way. A question about
     * where it is in the order rather than about how many stops there are: the
     * cursor clamps, so at the last stop `ring_fwd` is false and right is
     * dimmed - the honest answer, where "there is more than one focusable"
     * would light an arrow that moves nothing. */
    bool ring_back, ring_fwd;
    /* Whether a mixer column has been taken up. It is not a mode: A selects,
     * and on a page of values what A selects is a column - so this is one level
     * further down the same two words, and B is what lets go of it. What it
     * buys is that left and right go dead while a value is being changed, which
     * is the one place in the device where a sideways push that landed on the
     * neighbour would be silently wrong. */
    bool engaged;
    /* Whether a paged shape has another screenful either way. */
    bool page_back, page_fwd;
} catnip_dir_where;

/* What `button` means at `where`. Pure, and with no tree in it, so a host test
 * drives it by writing a situation down. */
catnip_dir_meaning catnip_ui_input_dir(catnip_button button, const catnip_dir_where *w);

/* The handle to focus after moving by `dir` (-1, 0 or +1) through `order`, the
 * live focusable handles the renderer hands back in tree order.
 *
 * Returns CATNIP_HANDLE_NONE when the order is empty. When `current` is not in
 * the order - the node it named was destroyed, or nothing is focused yet -
 * focus lands on the first entry and `dir` is ignored, because there is no
 * position to move from. Otherwise the cursor clamps at the ends rather than
 * wrapping: pushing up at the top list or down at the last button stays put, so
 * a held joystick does not cycle the focus round and round. */
catnip_handle catnip_ui_focus_step(const catnip_handle *order, int n,
                                   catnip_handle current, int dir);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_UI_INPUT_MAP_H */
