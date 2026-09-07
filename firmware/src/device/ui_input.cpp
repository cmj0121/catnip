/* ui_input.cpp - see ui_input.h. */
#include <lvgl.h>

#include "../catnip_render.h"
#include "input.h"
#include "lvgl_backend.h"
#include "touch.h"
#include "ui_input.h"
#include "ui_input_map.h"

namespace {

/* The most focusables a screen is expected to offer at once. A screen with more
 * than this still works; the cursor just walks the first kMaxFocus of them,
 * which on this panel is already more than fit on it. */
const int kMaxFocus = 32;

/* The LVGL pointer indev fed by the touch panel, created lazily once LVGL is up.
 * A tap on it lands on the button under the finger and fires LV_EVENT_CLICKED,
 * which is the same road a joystick activation takes: on_clicked in the backend
 * posts "click". So touch needs no code of its own here beyond reading the
 * panel into LVGL. */
lv_indev_t *g_touch_indev;

/* Where the focus ring is. A handle rather than an object, because the object
 * behind it can be created and destroyed as the tree changes and the handle is
 * what survives that - the renderer promises a handle resolves to nothing once
 * its node is gone, which is exactly what keeps a stale focus from lighting up
 * whatever reused the slot. */
catnip_handle g_focus = CATNIP_HANDLE_NONE;

void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x = 0, y = 0;

    (void)indev;
    /* The last known position outlives the touch (see touch.h), so the release
     * still reports where the finger was - which is the point LVGL clicks. */
    if (catnip_touch_position(&x, &y)) {
        data->point.x = (int32_t)x;
        data->point.y = (int32_t)y;
    }
    data->state = catnip_touch_down() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void ensure_touch_indev(void)
{
    if (g_touch_indev) return;
    /* LVGL comes up on the first widget an app draws, not at boot, so the indev
     * cannot be created until then. Until it is, touch does nothing, which is
     * correct: there is nothing on the panel to tap. */
    if (!catnip_lvgl_backend_active()) return;
    g_touch_indev = lv_indev_create();
    lv_indev_set_type(g_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_touch_indev, touch_read);
}

/* Post the event a pressed switch maps to, when it maps to one and something is
 * focused to receive it. Routed through catnip_ui_input_event so the names stay
 * the ones the host test pins. */
void post_if_event(catnip_rt *rt, catnip_button button)
{
    const char *event = catnip_ui_input_event(button);
    if (event && g_focus != CATNIP_HANDLE_NONE) catnip_render_post(rt, g_focus, event);
}

} /* namespace */

void catnip_ui_input_begin(void)
{
    /* The switches are begun by the HAL; only the touch panel is ours to bring
     * up, and it shares the I2C bus that is already up by now. A panel that does
     * not answer leaves every touch read reporting nothing, which the pointer
     * indev renders as a finger that is never down. */
    catnip_touch_begin();
}

bool catnip_ui_input_step(catnip_rt *rt)
{
    /* Read every edge once: the driver clears an edge as it is read, so this is
     * the sole reader and has to take them all here or lose them. */
    bool up = catnip_input_pressed(CATNIP_BTN_UP);
    bool down = catnip_input_pressed(CATNIP_BTN_DOWN);
    bool left = catnip_input_pressed(CATNIP_BTN_LEFT);
    bool right = catnip_input_pressed(CATNIP_BTN_RIGHT);
    bool centre = catnip_input_pressed(CATNIP_BTN_CENTRE);
    bool a = catnip_input_pressed(CATNIP_BTN_A);
    bool b = catnip_input_pressed(CATNIP_BTN_B);

    catnip_touch_poll();
    ensure_touch_indev();

    catnip_handle order[kMaxFocus];
    int n = catnip_render_focus_order(rt, order, kMaxFocus);
    if (n > kMaxFocus) n = kMaxFocus;

    /* Move the focus, then validate it: LEFT/RIGHT step the cursor, and the same
     * call resettles it on the first focusable when the node it was on is gone.
     * A pass with neither pressed still runs with dir 0, which is the validate.
     * The LEFT/RIGHT-to-direction rule is the map's, not re-derived here; both
     * pressed in one pass cancel to a stay, which is the honest thing to do. */
    int dir = 0;
    if (left) dir += catnip_ui_input_focus_dir(CATNIP_BTN_LEFT);
    if (right) dir += catnip_ui_input_focus_dir(CATNIP_BTN_RIGHT);
    g_focus = catnip_ui_focus_step(order, n, g_focus, dir);

    /* The ring goes on whatever is focused now. The backend moves it only when
     * the handle changed and dereferences the object itself, so this hands it a
     * handle and nothing more. */
    catnip_lvgl_backend_focus(g_focus);

    /* UP/DOWN move a list's selection; A and the centre activate. Nothing here
     * depends on the centre alone - A does the same - because #49 suspects the
     * centre's GPIO5 is really IR_RX and dead on this unit. */
    if (up) post_if_event(rt, CATNIP_BTN_UP);
    if (down) post_if_event(rt, CATNIP_BTN_DOWN);
    if (a) post_if_event(rt, CATNIP_BTN_A);
    if (centre) post_if_event(rt, CATNIP_BTN_CENTRE);

    /* B is back. It posts nothing - there is no node for it - so the caller acts
     * on the return value: leave the running app for the menu. */
    return b;
}

void catnip_ui_input_end(void)
{
    if (!g_touch_indev) return;
    lv_indev_delete(g_touch_indev);
    g_touch_indev = nullptr;
    /* The focus cursor is dropped too, so that if control ever came back the
     * ring would resettle from the top rather than point at a torn-down node. */
    g_focus = CATNIP_HANDLE_NONE;
}
