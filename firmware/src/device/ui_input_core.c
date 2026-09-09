/* ui_input_core.c - see ui_input_core.h. */
#include "ui_input_core.h"

#include "ui_input.h"
#include "ui_input_map.h"

/* Where an input event goes: the focused node, or the visible screen when
 * nothing is focused - which is what a canvas looks like, since a face has no
 * list on it and nothing to select.
 *
 * One home for the rule. It was written out at each of the three sites that
 * need it, each with its own copy of the same paragraph, which is the shape
 * that becomes four copies. */
static catnip_handle input_target(catnip_ui_input *in, catnip_rt *rt)
{
    if (in->focus != CATNIP_HANDLE_NONE) return in->focus;
    return catnip_render_visible_screen(rt);
}

static void post_if_event(catnip_ui_input *in, catnip_rt *rt, catnip_button button)
{
    const char *event = catnip_ui_input_event(button);
    catnip_handle to = input_target(in, rt);

    if (event && to != CATNIP_HANDLE_NONE)
        catnip_render_post(rt, to, event, CATNIP_INDEX_NONE);
}

/* A held switch asks about the *selected item*, so the event carries which row
 * that is. The renderer answers from the descriptor it already cached, so this
 * costs no Lua and runs nothing of the app's on the wrong side of the drain. A
 * focus that is not a list, or a list with nothing selected, sends the sentinel
 * and the handler simply finds no row - which is the honest answer when the
 * focus is on a button and there is no item to have options about. */
static void post_if_long_event(catnip_ui_input *in, catnip_rt *rt, catnip_button button)
{
    const char *event = catnip_ui_input_long_event(button);
    catnip_handle to = input_target(in, rt);

    if (!event || to == CATNIP_HANDLE_NONE) return;
    /* A screen has no selection to be about, and catnip_render_selected answers
     * the sentinel for one, so the index is right either way. */
    catnip_render_post(rt, to, event, catnip_render_selected(rt, to));
}

/* Whether the ring has anywhere to go either way. It clamps at the ends, so
 * this is a question about where it is in the order and not about how many
 * stops there are - at the last one, right moves nothing, and an arrow lit for
 * it would be the hint lying. */
static void focus_room(const catnip_handle *order, int n, catnip_handle focus, bool *back,
                       bool *fwd)
{
    int i, at = -1;

    for (i = 0; i < n; i++)
        if (order[i] == focus) at = i;
    /* Not in the order - nothing focused yet, or the node is gone - so the next
     * press lands on the first stop whichever way it went. */
    if (at < 0) {
        *back = *fwd = n > 0;
        return;
    }
    *back = at > 0;
    *fwd = at + 1 < n;
}

unsigned catnip_ui_input_hint(catnip_rt *rt, catnip_handle focus)
{
    catnip_handle order[CATNIP_UI_MAX_FOCUS];
    catnip_node_layout layout;
    unsigned events;
    unsigned mask = 0;
    bool back, fwd;
    int n;

    if (!rt) return 0;
    n = catnip_render_focus_order(rt, order, CATNIP_UI_MAX_FOCUS);
    if (n > CATNIP_UI_MAX_FOCUS) n = CATNIP_UI_MAX_FOCUS;
    focus_room(order, n, focus, &back, &fwd);
    layout = catnip_render_layout(rt, focus);
    events = catnip_render_events(rt, focus);

    if (catnip_ui_input_dir(CATNIP_BTN_UP, layout, events, back, fwd) !=
        CATNIP_DIR_NOTHING)
        mask |= CATNIP_HINT_UP;
    if (catnip_ui_input_dir(CATNIP_BTN_DOWN, layout, events, back, fwd) !=
        CATNIP_DIR_NOTHING)
        mask |= CATNIP_HINT_DOWN;
    if (catnip_ui_input_dir(CATNIP_BTN_LEFT, layout, events, back, fwd) !=
        CATNIP_DIR_NOTHING)
        mask |= CATNIP_HINT_LEFT;
    if (catnip_ui_input_dir(CATNIP_BTN_RIGHT, layout, events, back, fwd) !=
        CATNIP_DIR_NOTHING)
        mask |= CATNIP_HINT_RIGHT;
    return mask;
}

catnip_handle catnip_ui_input_focus(const catnip_ui_input *in)
{
    return in ? in->focus : CATNIP_HANDLE_NONE;
}

void catnip_ui_input_reset(catnip_ui_input *in)
{
    static const catnip_ui_input kZero = {0};
    if (in) *in = kZero;
}

int catnip_ui_input_run(catnip_ui_input *in, catnip_rt *rt, const catnip_ui_sample *s,
                        const catnip_ui_env *env)
{
    catnip_handle order[CATNIP_UI_MAX_FOCUS];
    int n;
    catnip_node_layout layout;
    unsigned events;
    int swipe;
    bool up, down, left, right;
    int a_press, c_press, b_press;
    bool step_back, step_fwd, dpad_up, dpad_down;
    bool dragged = false;
    catnip_dir_meaning m_up, m_down, m_left, m_right;

    if (!in || !rt || !s) return CATNIP_UI_GESTURE_NONE;

    /* What a direction means now comes from how long it has been held, so that
     * a value with sixty of something in it is not sixty presses. */
    up = catnip_repeat_step(&in->rep_up, s->down[CATNIP_BTN_UP], s->now);
    down = catnip_repeat_step(&in->rep_down, s->down[CATNIP_BTN_DOWN], s->now);
    left = catnip_repeat_step(&in->rep_left, s->down[CATNIP_BTN_LEFT], s->now);
    right = catnip_repeat_step(&in->rep_right, s->down[CATNIP_BTN_RIGHT], s->now);
    /* A, the centre and B are timed rather than edged: their meaning depends on
     * how long they are held, so what matters is the settled level. */
    a_press = catnip_press_step(&in->press_a, s->down[CATNIP_BTN_A], s->now);
    c_press = catnip_press_step(&in->press_centre, s->down[CATNIP_BTN_CENTRE], s->now);
    b_press = catnip_press_step(&in->press_b, s->down[CATNIP_BTN_B], s->now);

    /* Read the panel directly rather than through LVGL's own gesture: that one
     * only fires on an object nothing scrolled, is unreachable from a host
     * test, and its threshold is not ours to choose. */
    swipe = catnip_swipe_step(&in->swipe, s->touch_down, s->touch_x, s->touch_y);
    /* A contact that became a swipe is not a tap when it ends. LVGL sends a
     * click on any press-and-release over an object and does not care that the
     * finger travelled, so one drag would otherwise both move the selection and
     * activate the row it started on. */
    if (swipe != CATNIP_SWIPE_NONE && env && env->cancel_touch)
        env->cancel_touch(env->ud);

    n = catnip_render_focus_order(rt, order, CATNIP_UI_MAX_FOCUS);
    if (n > CATNIP_UI_MAX_FOCUS) n = CATNIP_UI_MAX_FOCUS;

    /* Settle the ring before anything is asked of it. A rebuild leaves the
     * handle it was on resolving to nothing, and everything below - what a
     * direction means, where a press goes - is a question about the node it is
     * resting on. Asked of a dead handle, the answers are all "nothing", which
     * is how the first press after coming back from an app went missing. */
    in->focus = catnip_ui_focus_step(order, n, in->focus, 0);

    /* What each direction means where the ring is. Asked before the ring moves,
     * because the meaning belongs to the node the press was made on - and asked
     * of ui_input_map.c rather than decided here, so the on-screen hint and this
     * cannot disagree about whether a direction does anything (#80). */
    layout = catnip_render_layout(rt, in->focus);
    events = catnip_render_events(rt, in->focus);
    {
        bool back, fwd;
        focus_room(order, n, in->focus, &back, &fwd);
        m_up = catnip_ui_input_dir(CATNIP_BTN_UP, layout, events, back, fwd);
        m_down = catnip_ui_input_dir(CATNIP_BTN_DOWN, layout, events, back, fwd);
        m_left = catnip_ui_input_dir(CATNIP_BTN_LEFT, layout, events, back, fwd);
        m_right = catnip_ui_input_dir(CATNIP_BTN_RIGHT, layout, events, back, fwd);
    }

    /* A carousel does not repeat. Repeat was added for a value with sixty of
     * something in it, where a step costs one number; on a carousel a step
     * hides one cell and shows another, and with a full-screen render mode that
     * is the whole panel blitted and any icon on it decoded again. Holding left
     * on the home ring would pin the CPU and the SPI bus at nine steps a
     * second, on battery, to walk past four apps. */
    if (m_left == CATNIP_DIR_RING_BACK) left = left && catnip_repeat_first(&in->rep_left);
    if (m_right == CATNIP_DIR_RING_FWD)
        right = right && catnip_repeat_first(&in->rep_right);

    step_back = left || swipe == CATNIP_SWIPE_LEFT;
    step_fwd = right || swipe == CATNIP_SWIPE_RIGHT;
    dpad_up = up || swipe == CATNIP_SWIPE_UP;
    dpad_down = down || swipe == CATNIP_SWIPE_DOWN;

    /* And now move it, if that is what sideways means here. Both directions in
     * one pass cancel to a stay, which is the honest thing to do. */
    {
        int dir = 0;
        if (step_back && m_left == CATNIP_DIR_FOCUS_BACK) dir -= 1;
        if (step_fwd && m_right == CATNIP_DIR_FOCUS_FWD) dir += 1;
        if (dir) in->focus = catnip_ui_focus_step(order, n, in->focus, dir);
    }

    /* A finger dragging over a column sets that column, absolutely. It is the
     * one gesture whose meaning is a place rather than a direction, so it is
     * the one that asks the environment where things are - and it is posted to
     * the *column* rather than to the list, because the answer names one column
     * and carries a percentage where an index would otherwise go.
     *
     * Only once the contact has travelled: a stationary touch is a tap, and a
     * tap on this page selects rather than sets. */
    if (!s->touch_down) {
        in->drag_col = CATNIP_HANDLE_NONE;
        in->drag_settled = false;
    } else if (!in->drag_settled && swipe != CATNIP_SWIPE_NONE) {
        /* The one pass on which this contact declares what it is. */
        in->drag_settled = true;
        if (m_up == CATNIP_DIR_RAISE &&
            (swipe == CATNIP_SWIPE_UP || swipe == CATNIP_SWIPE_DOWN) && env &&
            env->mixer_at) {
            catnip_handle col = CATNIP_HANDLE_NONE;
            int pct = 0;
            if (env->mixer_at(env->ud, s->touch_x, s->touch_y, &col, &pct))
                in->drag_col = col;
        }
    }
    if (in->drag_col != CATNIP_HANDLE_NONE && env && env->mixer_pct) {
        int pct = env->mixer_pct(env->ud, in->drag_col, s->touch_y);
        if (pct >= 0) {
            catnip_render_post(rt, in->drag_col, "drag", pct);
            dragged = true;
        }
    }

    /* And now what the directions asked for, each acted on by what it means
     * rather than by which switch it was. */
    if ((m_up == CATNIP_DIR_PREV && dpad_up) || (m_left == CATNIP_DIR_PREV && step_back))
        post_if_event(in, rt, CATNIP_BTN_UP);
    if ((m_down == CATNIP_DIR_NEXT && dpad_down) ||
        (m_right == CATNIP_DIR_NEXT && step_fwd))
        post_if_event(in, rt, CATNIP_BTN_DOWN);
    /* A carousel is stepped with the same two events a column gets from up and
     * down - only the direction that reaches them changes, because that is the
     * direction the arrangement makes obvious. */
    if (m_left == CATNIP_DIR_RING_BACK && step_back) post_if_event(in, rt, CATNIP_BTN_UP);
    if (m_right == CATNIP_DIR_RING_FWD && step_fwd)
        post_if_event(in, rt, CATNIP_BTN_DOWN);
    /* Up and down on a mixer are the value, not the selection, and they are
     * their own two names rather than prev/next with a different meaning
     * depending on the shape - a handler that had to ask what `next` meant this
     * time would be the wrong kind of clever.
     *
     * A drag has already said where the value goes; a step on top of it would
     * move it one further than the finger asked for. */
    if (m_up == CATNIP_DIR_RAISE && dpad_up && !dragged)
        catnip_render_post(rt, in->focus, "raise", CATNIP_INDEX_NONE);
    if (m_down == CATNIP_DIR_LOWER && dpad_down && !dragged)
        catnip_render_post(rt, in->focus, "lower", CATNIP_INDEX_NONE);

    if (a_press == CATNIP_PRESS_SHORT) post_if_event(in, rt, CATNIP_BTN_A);
    if (c_press == CATNIP_PRESS_SHORT) post_if_event(in, rt, CATNIP_BTN_CENTRE);
    if (a_press == CATNIP_PRESS_LONG) post_if_long_event(in, rt, CATNIP_BTN_A);
    if (c_press == CATNIP_PRESS_LONG) post_if_long_event(in, rt, CATNIP_BTN_CENTRE);

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
    if (m_down == CATNIP_DIR_LEAVE_DOWN && dpad_down) return CATNIP_UI_GESTURE_SETTINGS;

    return CATNIP_UI_GESTURE_NONE;
}
