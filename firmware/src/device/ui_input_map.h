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
