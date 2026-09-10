/* ui_input_core.c - see ui_input_core.h. */
#include "ui_input_core.h"

#include <string.h>

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

    if (!event || to == CATNIP_HANDLE_NONE) return;
    /* An activation may be answered with a list of actions, and the answer
     * comes back a drain later - so who was asked has to be remembered now.
     * Only the event that can be answered: `prev` and `next` reach this too,
     * and a page turned is not a question. */
    if (strcmp(event, "click") == 0) {
        in->options_on = to;
        in->options_index = catnip_render_selected(rt, to);
    }
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
    in->options_on = to;
    in->options_index = catnip_render_selected(rt, to);
    catnip_render_post(rt, to, event, in->options_index);
}

/* Whether a column is engaged, for the hint - which is asked by the frame with
 * no input state in its hand. Published rather than passed because the hint's
 * question is "what do the four directions do right now", and right now is a
 * property of the one input pass there is. */
static bool g_hint_engaged;

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

/* Where the ring is, as the one struct both the press and the arrow are asked
 * about. Written once so the two cannot be asked different questions - which is
 * the whole reason the meaning table is a function and not a switch in the
 * input pass. */
static void where_is(catnip_rt *rt, catnip_handle focus, bool engaged,
                     catnip_dir_where *w)
{
    catnip_handle order[CATNIP_UI_MAX_FOCUS];
    int n = catnip_render_focus_order(rt, order, CATNIP_UI_MAX_FOCUS);
    int shown = 0, total = 0, per, page;

    if (n > CATNIP_UI_MAX_FOCUS) n = CATNIP_UI_MAX_FOCUS;
    focus_room(order, n, focus, &w->ring_back, &w->ring_fwd);
    w->layout = catnip_render_layout(rt, focus);
    w->events = catnip_render_events(rt, focus);
    w->engaged = engaged && w->layout == CATNIP_LAYOUT_MIXER;
    w->page_back = false;
    w->page_fwd = false;

    /* How many pages the shape under the ring has, from the same counter the
     * bar draws - one derivation, so an arrow cannot be lit for a page the
     * header says does not exist. It answers in pages for exactly the shapes
     * that page, which is why nothing here has to know which those are. */
    per = w->layout == CATNIP_LAYOUT_GRID ? CATNIP_GRID_PAGE : CATNIP_MIXER_PAGE;
    (void)per;
    if (catnip_render_counter(rt, focus, &shown, &total) && total > 1) {
        page = shown; /* already one-based, and already a page for these shapes */
        w->page_back = page > 1;
        w->page_fwd = page < total;
    }
}

unsigned catnip_ui_input_hint(catnip_rt *rt, catnip_handle focus)
{
    catnip_dir_where w;
    unsigned mask = 0;

    if (!rt) return 0;
    where_is(rt, focus, g_hint_engaged, &w);

    if (catnip_ui_input_dir(CATNIP_BTN_UP, &w) != CATNIP_DIR_NOTHING)
        mask |= CATNIP_HINT_UP;
    if (catnip_ui_input_dir(CATNIP_BTN_DOWN, &w) != CATNIP_DIR_NOTHING)
        mask |= CATNIP_HINT_DOWN;
    if (catnip_ui_input_dir(CATNIP_BTN_LEFT, &w) != CATNIP_DIR_NOTHING)
        mask |= CATNIP_HINT_LEFT;
    if (catnip_ui_input_dir(CATNIP_BTN_RIGHT, &w) != CATNIP_DIR_NOTHING)
        mask |= CATNIP_HINT_RIGHT;
    return mask;
}

catnip_handle catnip_ui_input_options_target(const catnip_ui_input *in, int *index)
{
    if (index) *index = in ? in->options_index : CATNIP_INDEX_NONE;
    return in ? in->options_on : CATNIP_HANDLE_NONE;
}

catnip_handle catnip_ui_input_focus(const catnip_ui_input *in)
{
    return in ? in->focus : CATNIP_HANDLE_NONE;
}

void catnip_ui_input_reset(catnip_ui_input *in)
{
    static const catnip_ui_input kZero = {0};
    if (!in) return;
    *in = kZero;
    /* Zero is a live handle, so the two that hold one have to be said. */
    in->focus = CATNIP_HANDLE_NONE;
    in->drag_col = CATNIP_HANDLE_NONE;
    in->engaged_on = CATNIP_HANDLE_NONE;
    in->options_on = CATNIP_HANDLE_NONE;
    in->options_index = CATNIP_INDEX_NONE;
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

    /* The action bar, while one is up, owns A and B - and left and right too
     * when there are three of them, because three does not map onto two
     * buttons. Up and down are deliberately left alone: the content is still on
     * screen underneath, and an action is always about something the user can
     * still see.
     *
     * Handled before the ring is settled and before any meaning is worked out,
     * because none of that is what a press means right now. Long B still falls
     * through below: home is the platform's and no bar may swallow it. */
    if (env && catnip_bar_up(env->bar) && b_press != CATNIP_PRESS_LONG) {
        catnip_bar *bar = env->bar;
        int cell = -1;

        if (catnip_bar_modal(bar)) {
            if (left || swipe == CATNIP_SWIPE_LEFT) catnip_bar_step(bar, -1);
            if (right || swipe == CATNIP_SWIPE_RIGHT) catnip_bar_step(bar, 1);
        }
        /* A finger runs a cell directly, in both shapes. It is the only way a
         * touch user reaches Cancel, which is why the bar draws a visible one
         * at all. */
        if (!s->touch_down && in->touch_was_down && !in->drag_settled && env->action_at)
            cell = env->action_at(env->ud, s->touch_x, s->touch_y);
        in->touch_was_down = s->touch_down;

        if (cell >= 0) {
            catnip_render_post_action(rt, bar->owner, bar->index,
                                      catnip_bar_tap(bar, cell));
            catnip_bar_close(bar);
        } else if (a_press == CATNIP_PRESS_SHORT || c_press == CATNIP_PRESS_SHORT) {
            catnip_render_post_action(rt, bar->owner, bar->index,
                                      catnip_bar_activate(bar));
            catnip_bar_close(bar);
        } else if (b_press == CATNIP_PRESS_SHORT) {
            /* B runs the negative one when there is one and it is safe to bind
             * B to, and otherwise only puts the bar away. Either way the bar
             * goes: B is how you leave, and leaving is the one thing it must
             * always do. */
            const char *id = catnip_bar_cancel(bar);
            if (id) catnip_render_post_action(rt, bar->owner, bar->index, id);
            catnip_bar_close(bar);
        }
        return CATNIP_UI_GESTURE_NONE;
    }

    n = catnip_render_focus_order(rt, order, CATNIP_UI_MAX_FOCUS);
    if (n > CATNIP_UI_MAX_FOCUS) n = CATNIP_UI_MAX_FOCUS;

    /* Settle the ring before anything is asked of it. A rebuild leaves the
     * handle it was on resolving to nothing, and everything below - what a
     * direction means, where a press goes - is a question about the node it is
     * resting on. Asked of a dead handle, the answers are all "nothing", which
     * is how the first press after coming back from an app went missing. */
    in->focus = catnip_ui_focus_step(order, n, in->focus, 0);

    /* A column stays taken up only while the ring is still on it and the shape
     * under it is still a page of values. Both can stop being true without a
     * press - an app rebuilds, a screen is popped - and a level that outlived
     * what it was about would send the next up-press into a value nobody is
     * looking at. */
    layout = catnip_render_layout(rt, in->focus);
    events = catnip_render_events(rt, in->focus);
    if (in->engaged && (in->focus != in->engaged_on || layout != CATNIP_LAYOUT_MIXER))
        in->engaged = false;

    /* What each direction means where the ring is. Asked before the ring moves,
     * because the meaning belongs to the node the press was made on - and asked
     * of ui_input_map.c rather than decided here, so the on-screen hint and this
     * cannot disagree about whether a direction does anything (#80). */
    {
        catnip_dir_where w;
        where_is(rt, in->focus, in->engaged, &w);
        g_hint_engaged = in->engaged;
        m_up = catnip_ui_input_dir(CATNIP_BTN_UP, &w);
        m_down = catnip_ui_input_dir(CATNIP_BTN_DOWN, &w);
        m_left = catnip_ui_input_dir(CATNIP_BTN_LEFT, &w);
        m_right = catnip_ui_input_dir(CATNIP_BTN_RIGHT, &w);
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
        if (dir) {
            in->focus = catnip_ui_focus_step(order, n, in->focus, dir);
            in->engaged = false;
        }
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
        /* A contact that ended without ever becoming a drag is a tap, and a tap
         * on a page of values takes that column up - the same thing A does,
         * said with a finger. It has to be noticed here because the tap itself
         * goes straight from the panel to the app, carrying the column it
         * landed on; the level is the platform's, and this is the only place
         * that sees the contact end.
         *
         * Twice quickly is `save`, for the same reason two presses of A are:
         * "and I am done" is a thing a finger has to be able to say too. */
        if (in->touch_was_down && !in->drag_settled && layout == CATNIP_LAYOUT_MIXER) {
            bool twice =
                in->had_tap && (unsigned)(s->now - in->last_tap) < CATNIP_DOUBLE_MS;
            if (twice && in->engaged) {
                catnip_render_post(rt, in->focus, "save", CATNIP_INDEX_NONE);
                in->engaged = false;
                in->had_tap = false;
            } else if (!in->engaged) {
                in->engaged = true;
                in->engaged_on = in->focus;
                in->had_tap = true;
                in->last_tap = s->now;
                catnip_render_post(rt, in->focus, "engage",
                                   catnip_render_selected(rt, in->focus));
            }
        }
        in->touch_was_down = false;
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
    if (s->touch_down) in->touch_was_down = true;
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

    /* A page is a screenful of the selection, said in the vocabulary that
     * already exists: the selection is what the page is derived from, so moving
     * it by a page *is* the page turning. A new event would have been a second
     * way to say a thing prev and next already say, and every app that had one
     * would have had to answer both. The app's own clamp at the ends is what
     * stops a short last page walking off it. */
    if (m_up == CATNIP_DIR_PAGE_BACK && dpad_up)
        for (int k = 0; k < CATNIP_MIXER_PAGE; k++)
            catnip_render_post(rt, in->focus, "prev", CATNIP_INDEX_NONE);
    if (m_down == CATNIP_DIR_PAGE_FWD && dpad_down)
        for (int k = 0; k < CATNIP_MIXER_PAGE; k++)
            catnip_render_post(rt, in->focus, "next", CATNIP_INDEX_NONE);

    /* A on a page of values, which is the one shape where A has two things to
     * mean and therefore somewhere for a double press to fit.
     *
     * Unfocused it takes the column up and posts `engage`, which is the app's
     * chance to remember what the column held - because B is going to be able
     * to put it back. Focused it posts `click`, which is the same "activate"
     * A means everywhere, and lets go. Twice in quick succession it posts
     * `save`: keep all of it and leave, which the app does by leaving.
     *
     * Nothing about levels reaches an app that is not a page of values, and an
     * app with no `save` handler simply has no double press. */
    /* Anything else at all closes the window a double press lives in. "Twice"
     * means twice *with nothing between*, which is what a person doing it means
     * by it - and it is what keeps the ordinary sequence readable: taking a
     * column up, stepping it, and pressing A to keep it is three presses of
     * which two are A, and without this it would be a double press with a
     * joystick push hidden in the middle of it. */
    if (dpad_up || dpad_down || step_back || step_fwd || dragged) {
        in->had_a = false;
        in->had_tap = false;
    }

    if (a_press == CATNIP_PRESS_SHORT || c_press == CATNIP_PRESS_SHORT) {
        bool mixer = layout == CATNIP_LAYOUT_MIXER;
        bool twice = in->had_a && (unsigned)(s->now - in->last_a) < CATNIP_DOUBLE_MS;

        if (mixer && in->engaged && twice) {
            catnip_render_post(rt, in->focus, "save", CATNIP_INDEX_NONE);
            in->engaged = false;
            in->had_a = false;
        } else if (mixer && !in->engaged) {
            in->engaged = true;
            in->engaged_on = in->focus;
            /* Only an engage arms the second press. A commit does not, so
             * keeping one column and then taking up the next is two presses of
             * A that are never mistaken for one gesture. */
            in->had_a = true;
            in->last_a = s->now;
            catnip_render_post(rt, in->focus, "engage",
                               catnip_render_selected(rt, in->focus));
        } else if (mixer) {
            catnip_render_post(rt, in->focus, "click", CATNIP_INDEX_NONE);
            in->engaged = false;
            in->had_a = false;
        } else {
            in->had_a = false;
            if (a_press == CATNIP_PRESS_SHORT) post_if_event(in, rt, CATNIP_BTN_A);
            if (c_press == CATNIP_PRESS_SHORT) post_if_event(in, rt, CATNIP_BTN_CENTRE);
        }
    }
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
        catnip_handle screen;

        /* B one level down: it puts this column back and lets go, and the page
         * stays. Cancel means the same thing it means everywhere - undo the
         * thing you are on - and on a column the thing you are on is a value.
         * It is why the level costs nothing to learn, and why nothing on the
         * screen has to say which level you are at. */
        if (in->engaged) {
            catnip_render_post(rt, in->focus, "cancel", CATNIP_INDEX_NONE);
            in->engaged = false;
            return CATNIP_UI_GESTURE_NONE;
        }
        screen = catnip_render_visible_screen(rt);
        if (screen != CATNIP_HANDLE_NONE)
            catnip_render_post_claimable(rt, screen, "back", CATNIP_INDEX_NONE);
        return CATNIP_UI_GESTURE_BACK;
    }

    /* After B, so a pass that carries both leaves rather than descends: getting
     * out is the gesture that must never be the one that loses. */
    if (m_down == CATNIP_DIR_LEAVE_DOWN && dpad_down) return CATNIP_UI_GESTURE_SETTINGS;
    if (m_up == CATNIP_DIR_LEAVE_UP && dpad_up) return CATNIP_UI_GESTURE_GRID;

    return CATNIP_UI_GESTURE_NONE;
}
