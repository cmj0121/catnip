/* ui_input.cpp - see ui_input.h. */
#include <Arduino.h>
#include <lvgl.h>

#include "../catnip_render.h"
#include "input.h"
#include "lvgl_backend.h"
#include "key_repeat.h"
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

/* The mixer column a drag has taken hold of, and whether this contact has been
 * classified yet.
 *
 * A drag keeps the column it started on. Without that, a finger sweeping
 * sideways set every column it crossed to whatever height it happened to be at
 * - one left-swipe changed four settings at once. It is also why only a
 * contact that began by moving *up or down* becomes a drag at all: a sideways
 * one is the gesture for changing which column is under the ring, and it must
 * not also be the gesture for changing a value. */
catnip_handle g_drag_col = CATNIP_HANDLE_NONE;
bool g_drag_settled;

/* A held direction keeps going. Four states, one per direction, because they
 * are four separate keys and holding one must not arm another. */
catnip_repeat g_rep_up, g_rep_down, g_rep_left, g_rep_right;

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
/* Where an input event goes: the focused node, or the visible screen when
 * nothing is focused - which is what a canvas looks like, since a face has no
 * list on it and nothing to select.
 *
 * One home for the rule. It was written out at each of the three sites that
 * need it, each with its own copy of the same paragraph, which is the shape
 * that becomes four copies. */
catnip_handle input_target(catnip_rt *rt)
{
    if (g_focus != CATNIP_HANDLE_NONE) return g_focus;
    return catnip_render_visible_screen(rt);
}

void post_if_event(catnip_rt *rt, catnip_button button)
{
    const char *event = catnip_ui_input_event(button);
    catnip_handle to = input_target(rt);

    if (event && to != CATNIP_HANDLE_NONE)
        catnip_render_post(rt, to, event, CATNIP_INDEX_NONE);
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
    catnip_handle to = input_target(rt);

    if (!event || to == CATNIP_HANDLE_NONE) return;
    /* A screen has no selection to be about, and catnip_render_selected answers
     * the sentinel for one, so the index is right either way. */
    catnip_render_post(rt, to, event, catnip_render_selected(rt, to));
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
    /* The edges are still read, because the driver clears them as they are read
     * and this is their sole reader - but what a direction *means* now comes
     * from how long it has been held, so that a value with sixty of something
     * in it is not sixty presses. */
    (void)catnip_input_pressed(CATNIP_BTN_UP);
    (void)catnip_input_pressed(CATNIP_BTN_DOWN);
    (void)catnip_input_pressed(CATNIP_BTN_LEFT);
    (void)catnip_input_pressed(CATNIP_BTN_RIGHT);
    /* A, the centre and B are timed rather than edged: their meaning depends on
     * how long they are held, so what matters is the settled level, and the
     * edges are read only to keep this the sole reader of them. */
    (void)catnip_input_pressed(CATNIP_BTN_CENTRE);
    (void)catnip_input_pressed(CATNIP_BTN_A);
    (void)catnip_input_pressed(CATNIP_BTN_B);
    unsigned now = (unsigned)millis();
    bool up = catnip_repeat_step(&g_rep_up, catnip_input_down(CATNIP_BTN_UP), now);
    bool down = catnip_repeat_step(&g_rep_down, catnip_input_down(CATNIP_BTN_DOWN), now);
    bool left = catnip_repeat_step(&g_rep_left, catnip_input_down(CATNIP_BTN_LEFT), now);
    bool right =
        catnip_repeat_step(&g_rep_right, catnip_input_down(CATNIP_BTN_RIGHT), now);
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
    int layout = catnip_render_layout(rt, g_focus);
    bool carousel = layout == CATNIP_LAYOUT_CAROUSEL;
    /* A mixer is stepped sideways for the same reason a carousel is - its
     * children are laid out across the region - but the axis it does not use
     * for that is not spare: up and down are the value of the column under the
     * ring. So the two shapes share "left and right move the selection" and
     * part company on what up and down mean. */
    bool sideways = carousel || layout == CATNIP_LAYOUT_MIXER;
    /* A carousel does not repeat. Repeat was added for a value with sixty of
     * something in it, where a step costs one number; on a carousel a step
     * hides one cell and shows another, and with a full-screen render mode that
     * is the whole panel blitted and any icon on it decoded again. Holding left
     * on the home ring would pin the CPU and the SPI bus at nine steps a
     * second, on battery, to walk past four apps. */
    bool left_edge = carousel ? (left && catnip_repeat_first(&g_rep_left)) : left;
    bool right_edge = carousel ? (right && catnip_repeat_first(&g_rep_right)) : right;
    bool step_back = left_edge || swipe == CATNIP_SWIPE_LEFT;
    bool step_fwd = right_edge || swipe == CATNIP_SWIPE_RIGHT;

    int dir = 0;
    if (!sideways) {
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
    /* On a carousel the four directions mean four different things, which is
     * what makes it the home section rather than a list laid out sideways:
     * left and right step through it, and up and down leave it for the planes
     * either side. So up and down are not posted here at all - a carousel that
     * also stepped on down would move under the finger that was asking to go
     * to the settings page. */
    bool dpad_up = up || swipe == CATNIP_SWIPE_UP;
    bool dpad_down = down || swipe == CATNIP_SWIPE_DOWN;
    bool mixer = layout == CATNIP_LAYOUT_MIXER;

    /* A finger dragging over a column sets that column, absolutely. It is the
     * one gesture whose meaning is a place rather than a direction, so it is
     * the one that asks the backend where things are - and it is posted to the
     * *column* rather than to the list, because the answer names one column and
     * carries a percentage where an index would otherwise go.
     *
     * Only once the contact has travelled: a stationary touch is a tap, and a
     * tap on this page selects rather than sets. */
    bool dragged = false;
    if (!catnip_touch_down()) {
        g_drag_col = CATNIP_HANDLE_NONE;
        g_drag_settled = false;
    } else if (!g_drag_settled && swipe != CATNIP_SWIPE_NONE) {
        /* The one pass on which this contact declares what it is. */
        g_drag_settled = true;
        if (mixer && (swipe == CATNIP_SWIPE_UP || swipe == CATNIP_SWIPE_DOWN)) {
            catnip_handle col = CATNIP_HANDLE_NONE;
            int pct = 0;
            if (catnip_lvgl_backend_mixer_at((int)tx, (int)ty, &col, &pct))
                g_drag_col = col;
        }
    }
    if (mixer && g_drag_col != CATNIP_HANDLE_NONE) {
        int pct = catnip_lvgl_backend_mixer_pct(g_drag_col, (int)ty);
        if (pct >= 0) {
            catnip_render_post(rt, g_drag_col, "drag", pct);
            dragged = true;
        }
    }
    if ((sideways && step_back) || (!sideways && dpad_up))
        post_if_event(rt, CATNIP_BTN_UP);
    if ((sideways && step_fwd) || (!sideways && dpad_down))
        post_if_event(rt, CATNIP_BTN_DOWN);
    /* Up and down on a mixer are the value, not the selection, and they are
     * their own two names rather than prev/next with a different meaning
     * depending on the shape - a handler that had to ask what `next` meant
     * this time would be the wrong kind of clever. */
    /* A drag has already said where the value goes; a step on top of it would
     * move it one further than the finger asked for. */
    if (mixer && !dragged && dpad_up)
        catnip_render_post(rt, g_focus, "raise", CATNIP_INDEX_NONE);
    if (mixer && !dragged && dpad_down)
        catnip_render_post(rt, g_focus, "lower", CATNIP_INDEX_NONE);
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

    /* After B, so a pass that carries both leaves rather than descends: getting
     * out is the gesture that must never be the one that loses. */
    if (carousel && dpad_down) return CATNIP_UI_GESTURE_SETTINGS;

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
    g_drag_col = CATNIP_HANDLE_NONE;
    g_drag_settled = false;
    /* And a direction still held here must not carry its start time into
     * whatever comes next, for the reason press_gesture.h gives. */
    g_rep_up = g_rep_down = g_rep_left = g_rep_right = catnip_repeat{};
}
