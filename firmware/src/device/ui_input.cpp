/* ui_input.cpp - see ui_input.h. */
#include <Arduino.h>
#include <lvgl.h>

#include "../catnip_render.h"
#include "input.h"
#include "lvgl_backend.h"
#include "press_gesture.h"
#include "swipe.h"
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

/* One contact tracker per switch that has two meanings. A and the centre carry
 * click/options; B carries back/home. The joystick's four directions have one
 * meaning each and stay on the plain edge, because a held direction that waited
 * 600 ms to decide would make the list feel stuck. */
catnip_press g_press_a, g_press_centre, g_press_b;

/* The finger, read as one of the joystick's four directions. It is the same
 * vocabulary and not a second one, so what it produces below is exactly what
 * the corresponding switch produces - no event of its own, and no meaning the
 * joystick does not already have. */
catnip_swipe g_swipe;

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
    if (event && g_focus != CATNIP_HANDLE_NONE)
        catnip_render_post(rt, g_focus, event, CATNIP_INDEX_NONE);
}

/* A held switch asks about the *selected item*, so the event carries which row
 * that is. The renderer answers from the descriptor it already cached, so this
 * costs no Lua and runs nothing of the app's on the wrong side of the drain. A
 * focus that is not a list, or a list with nothing selected, sends the sentinel
 * and the handler simply finds no row - which is the honest answer when the
 * focus is on a button and there is no item to have options about. */
void post_if_long_event(catnip_rt *rt, catnip_button button)
{
    const char *event = catnip_ui_input_long_event(button);
    if (!event || g_focus == CATNIP_HANDLE_NONE) return;
    catnip_render_post(rt, g_focus, event, catnip_render_selected(rt, g_focus));
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

int catnip_ui_input_step(catnip_rt *rt)
{
    /* Read every edge once: the driver clears an edge as it is read, so this is
     * the sole reader and has to take them all here or lose them. */
    bool up = catnip_input_pressed(CATNIP_BTN_UP);
    bool down = catnip_input_pressed(CATNIP_BTN_DOWN);
    bool left = catnip_input_pressed(CATNIP_BTN_LEFT);
    bool right = catnip_input_pressed(CATNIP_BTN_RIGHT);
    /* A, the centre and B are timed rather than edged: their meaning depends on
     * how long they are held, so what matters is the settled level, and the
     * edges are read only to keep this the sole reader of them. */
    (void)catnip_input_pressed(CATNIP_BTN_CENTRE);
    (void)catnip_input_pressed(CATNIP_BTN_A);
    (void)catnip_input_pressed(CATNIP_BTN_B);
    unsigned now = (unsigned)millis();
    int a_press = catnip_press_step(&g_press_a, catnip_input_down(CATNIP_BTN_A), now);
    int c_press =
        catnip_press_step(&g_press_centre, catnip_input_down(CATNIP_BTN_CENTRE), now);
    int b_press = catnip_press_step(&g_press_b, catnip_input_down(CATNIP_BTN_B), now);

    catnip_touch_poll();
    ensure_touch_indev();

    /* Read the panel directly rather than through LVGL's own gesture: that one
     * only fires on an object nothing scrolled, is unreachable from a host
     * test, and its threshold is not ours to choose. */
    uint16_t tx = 0, ty = 0;
    (void)catnip_touch_position(&tx, &ty);
    int swipe = catnip_swipe_step(&g_swipe, catnip_touch_down(), (int)tx, (int)ty);
    /* A contact that became a swipe is not a tap when it ends. LVGL sends a
     * click on any press-and-release over an object and does not care that the
     * finger travelled, so one drag would otherwise both move the selection and
     * activate the row it started on.
     *
     * Told to the indev rather than gated in the backend: this ends the contact
     * as far as LVGL is concerned - it sends PRESS_LOST, forgets the object and
     * emits no click - which covers every clickable object rather than the two
     * whose callbacks anyone remembered to guard. */
    if (swipe != CATNIP_SWIPE_NONE && g_touch_indev) lv_indev_wait_release(g_touch_indev);

    catnip_handle order[kMaxFocus];
    int n = catnip_render_focus_order(rt, order, kMaxFocus);
    if (n > kMaxFocus) n = kMaxFocus;

    /* Move the focus, then validate it: LEFT/RIGHT step the cursor, and the same
     * call resettles it on the first focusable when the node it was on is gone.
     * A pass with neither pressed still runs with dir 0, which is the validate.
     * The LEFT/RIGHT-to-direction rule is the map's, not re-derived here; both
     * pressed in one pass cancel to a stay, which is the honest thing to do. */
    /* Sideways means two things, and which one is decided by the shape on
     * screen rather than by a mode: a carousel is stepped left and right, so
     * left and right move its selection; anywhere else they move the focus
     * between the focusable things. The events are the same `prev` and `next`
     * a column gets from up and down - only the direction that reaches them
     * changes, because that is the direction the arrangement makes obvious. */
    bool carousel = catnip_render_layout(rt, g_focus) == CATNIP_LAYOUT_CAROUSEL;
    bool step_back = left || swipe == CATNIP_SWIPE_LEFT;
    bool step_fwd = right || swipe == CATNIP_SWIPE_RIGHT;

    int dir = 0;
    if (!carousel) {
        if (step_back) dir += catnip_ui_input_focus_dir(CATNIP_BTN_LEFT);
        if (step_fwd) dir += catnip_ui_input_focus_dir(CATNIP_BTN_RIGHT);
    }
    g_focus = catnip_ui_focus_step(order, n, g_focus, dir);

    /* The ring goes on whatever is focused now. The backend moves it only when
     * the handle changed and dereferences the object itself, so this hands it a
     * handle and nothing more. */
    catnip_lvgl_backend_focus(g_focus);

    /* UP/DOWN move a list's selection; A and the centre activate when tapped and
     * ask for the item's options when held. Nothing here depends on the centre
     * alone - A does the same - because #49 suspects the centre's GPIO5 is
     * really IR_RX and dead on this unit. */
    if (up || swipe == CATNIP_SWIPE_UP || (carousel && step_back))
        post_if_event(rt, CATNIP_BTN_UP);
    if (down || swipe == CATNIP_SWIPE_DOWN || (carousel && step_fwd))
        post_if_event(rt, CATNIP_BTN_DOWN);
    if (a_press == CATNIP_PRESS_SHORT) post_if_event(rt, CATNIP_BTN_A);
    if (c_press == CATNIP_PRESS_SHORT) post_if_event(rt, CATNIP_BTN_CENTRE);
    if (a_press == CATNIP_PRESS_LONG) post_if_long_event(rt, CATNIP_BTN_A);
    if (c_press == CATNIP_PRESS_LONG) post_if_long_event(rt, CATNIP_BTN_CENTRE);

    /* Holding B is home, and home is the platform's alone: it is not posted, no
     * app code runs before it takes effect, and no handler can swallow it. That
     * is what makes it the escape a user can rely on - checked before back, so
     * a gesture that became long is never also reported as short. */
    if (b_press == CATNIP_PRESS_LONG) return CATNIP_UI_GESTURE_HOME;

    /* A short B is back, and back is a negotiation. It is posted to the visible
     * screen so the app's on_back runs in the drain that follows this call; the
     * caller reads the answer afterwards. An app with no on_back posts into
     * nothing and claims nothing, which is how "B leaves it" stays the default. */
    if (b_press == CATNIP_PRESS_SHORT) {
        catnip_handle screen = catnip_render_visible_screen(rt);
        if (screen != CATNIP_HANDLE_NONE)
            catnip_render_post_claimable(rt, screen, "back", CATNIP_INDEX_NONE);
        return CATNIP_UI_GESTURE_BACK;
    }

    return CATNIP_UI_GESTURE_NONE;
}

void catnip_ui_input_end(void)
{
    if (!g_touch_indev) return;
    lv_indev_delete(g_touch_indev);
    g_touch_indev = nullptr;
    /* The focus cursor is dropped too, so that if control ever came back the
     * ring would resettle from the top rather than point at a torn-down node. */
    g_focus = CATNIP_HANDLE_NONE;
    /* And so is every contact in flight, for the reason press_gesture.h gives:
     * a switch still held here must not carry its start time into whatever
     * comes next. */
    g_press_a = g_press_centre = g_press_b = catnip_press{};
    g_swipe = catnip_swipe{};
}
