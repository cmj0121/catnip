/*
 * ui_input.h - physical input into the ui.* event model (#31).
 *
 * This is the wiring the convention in ui_input_map.h describes: it reads the
 * seven switches and the touch panel and turns them into the events an app's
 * on_* handlers run against. The switches drive a focus cursor over the
 * renderer's own focus order and post prev / next / click to whatever is
 * focused; the touch panel feeds an LVGL pointer indev, so a tap lands on the
 * button under the finger and activates it through the same on_click path.
 *
 * It posts through catnip_render_post() and never runs a handler itself - the
 * handlers run in catnip_render_drain(), on the watchdogged coroutine, which is
 * the discipline the whole renderer rests on. So this can be called straight
 * from the main loop with no more care than any other poll.
 *
 * WHY A FOCUS CURSOR RATHER THAN AN lv_group. The renderer already owns the
 * focus order (catnip_render_focus_order), rebuilt as the tree changes, and it
 * hands it back in handles. Driving an lv_group instead would mean tearing that
 * group down and rebuilding it every pass to stay in step - duplicating the
 * ownership and losing the focus across the rebuild - so the cursor walks the
 * renderer's order directly and only borrows LVGL for the one thing it is the
 * authority on: painting the focus ring, via LV_STATE_FOCUSED on the object.
 */
#ifndef CATNIP_UI_INPUT_H
#define CATNIP_UI_INPUT_H

#include <stdbool.h>

#include "../catnip_render.h"
#include "../catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Confirm the touch panel and get ready to feed LVGL. The switches are begun by
 * the HAL, so this only owns the touch half. Call once in setup(), after
 * catnip_i2c_begin(); the LVGL pointer indev is created later, on the first
 * pass that finds LVGL up, because LVGL comes up on the first widget an app
 * draws and not before. */
/* What one pass of the switches asked for beyond the events it posted. Both are
 * B: pressed, it is back; held, it is home. Neither is delivered to a node,
 * because there is no node for "leave this app" - the caller acts on it.
 *
 * They are separate values rather than a flag on back, because they are not the
 * same request with more urgency. Back is a negotiation the app can win by
 * returning truthy from on_back; home is not a negotiation at all, and no app
 * code runs before it takes effect. */
enum {
    CATNIP_UI_GESTURE_NONE = 0,
    CATNIP_UI_GESTURE_BACK,
    CATNIP_UI_GESTURE_HOME,
    /* Down, on the home section's carousel: the settings plane (#67). It is a
     * gesture and not an event posted to a node because there is no node for
     * "the settings page" until the platform makes one - the same reason back
     * and home leave through here. Only a carousel produces it; in a column
     * down is still the next row, which is why a long list cannot fall into
     * the settings page by being scrolled too far. */
    CATNIP_UI_GESTURE_SETTINGS,
};

void catnip_ui_input_begin(void);

/* One pass of UI input against the tree `rt` is rendering. Reads the switches
 * and the touch panel, moves the focus cursor with the joystick's LEFT/RIGHT,
 * posts prev / next / click / options to the focused node, and paints the focus
 * ring. Returns one of the CATNIP_UI_GESTURE_* values above for what B asked.
 *
 * A short B posts `back` to the visible screen on its way out, so that the
 * app's on_back runs in the very next drain and the caller can read the answer
 * with catnip_render_take_claim() in the same pass. Posting it here rather than
 * leaving it to the caller is what keeps that one-pass ordering true; the
 * caller still decides what a declined back means.
 *
 * Call it once per loop, before catnip_render_drain(), so a press is delivered
 * in the same frame it was made. It reads each switch edge exactly once, so
 * nothing else may read the switches in the same pass. */
int catnip_ui_input_step(catnip_rt *rt);

/* Give the touch panel back: delete the LVGL pointer indev so nothing here
 * feeds LVGL any more. The diagnostic page (#42) owns the panel and reads the
 * touch driver directly, so a lingering indev injecting pointer events onto its
 * screen would be a second hand on the wheel. enter_diag() calls this as part
 * of the same handoff that tears the app tree down. A no-op when no indev was
 * ever created, which is the boot-marker path into diag. */
void catnip_ui_input_end(void);

/* Where the focus ring is. The control hint asks, because what a direction does
 * depends on what the ring is resting on (#80). */
catnip_handle catnip_ui_input_focused(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_UI_INPUT_H */
