/*
 * ui_input_core.h - the input pass, with no board under it (#81).
 *
 * ui_input.cpp used to be one function: read the switches, read the panel,
 * decide what a press meant, post it, move the focus ring, and report what B
 * asked for. Only the first two of those need a board, and the rest were
 * unreachable from a host test because they sat behind `#include <Arduino.h>`.
 *
 * That is where several days of bugs came from. A press that reached the wrong
 * node, a focus ring resting on something that would not answer it, a long B
 * that went to the wrong place - every one of them was found by a person
 * holding the device and describing what they saw, which is the most expensive
 * test in the project. None of them needed an ESP32 to reproduce.
 *
 * So the decisions live here, in plain C: given which switches are down, where
 * the finger is and what time it is, this posts to the tree, moves the cursor
 * and returns the gesture. ui_input.cpp is the half that reads a GPIO.
 *
 * Two things genuinely cannot come along, and both are geometry: where a mixer
 * column is on the glass, and telling LVGL's pointer that a contact became a
 * swipe and is therefore not a tap. They are asked for through `catnip_ui_env`,
 * which a host leaves empty - a drag is a gesture whose meaning is a *place*,
 * and a place needs something that draws.
 */
#ifndef CATNIP_UI_INPUT_CORE_H
#define CATNIP_UI_INPUT_CORE_H

#include <stdbool.h>

#include "../catnip_render.h"
#include "../catnip_runtime.h"
#include "input.h"
#include "key_repeat.h"
#include "press_gesture.h"
#include "swipe.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The most focusables a screen is expected to offer at once. A screen with more
 * than this still works; the cursor just walks the first of them, which on this
 * panel is already more than fit on it. */
#define CATNIP_UI_MAX_FOCUS 32

/* What the world looked like on this pass. Everything the decisions below rest
 * on, and nothing else - which is what makes a host test able to state a
 * situation rather than simulate a device. */
typedef struct {
    bool down[CATNIP_BTN_COUNT];
    bool touch_down;
    int touch_x, touch_y;
    unsigned now; /* a monotonic millisecond clock */
} catnip_ui_sample;

/* The two questions that need something that draws, and the object they are
 * asked of. Every field may be NULL: a run with none of them still delivers
 * every switch, and only the drag - which is geometry by definition - is
 * missing. */
typedef struct {
    /* Which mixer column is under (x, y), and how far up it the finger is. */
    int (*mixer_at)(void *ud, int x, int y, catnip_handle *col, int *pct);
    /* How far up `col` the y coordinate is, as a percentage, or -1. */
    int (*mixer_pct)(void *ud, catnip_handle col, int y);
    /* This contact has become a swipe, so it must not also arrive as a tap. */
    void (*cancel_touch)(void *ud);
    void *ud;
} catnip_ui_env;

/* Everything one input pass has to remember between passes: how long each
 * switch has been held, whether a contact has been classified, and where the
 * ring is. Transparent so it can be a static or a stack object, like the
 * catnip_press and catnip_repeat it is made of. Zero it to start. */
typedef struct {
    catnip_press press_a, press_centre, press_b;
    catnip_repeat rep_up, rep_down, rep_left, rep_right;
    catnip_swipe swipe;
    /* Where the focus ring is. A handle rather than an object, because the
     * object behind it can be created and destroyed as the tree changes and the
     * handle is what survives that - the renderer promises a handle resolves to
     * nothing once its node is gone, which is exactly what keeps a stale focus
     * from lighting up whatever reused the slot. */
    catnip_handle focus;
    /* The mixer column a drag has taken hold of, and whether this contact has
     * been classified yet. A drag keeps the column it started on: without that,
     * a finger sweeping sideways set every column it crossed, and one
     * left-swipe changed four settings at once. */
    catnip_handle drag_col;
    bool drag_settled;
} catnip_ui_input;

/* One pass. Posts prev / next / click / options to the focused node, moves the
 * focus cursor, and returns one of the CATNIP_UI_GESTURE_* values in
 * ui_input.h for what B or the ring asked of the caller.
 *
 * `env` may be NULL. The focus ring itself is not painted here - that is LVGL's
 * one authority in this arrangement - so the caller reads
 * catnip_ui_input_focus() afterwards and hands it to whatever draws. */
int catnip_ui_input_run(catnip_ui_input *in, catnip_rt *rt, const catnip_ui_sample *s,
                        const catnip_ui_env *env);

/* Where the ring is, for whoever paints it and for the control hint, which asks
 * what the focused node is listening to. */
catnip_handle catnip_ui_input_focus(const catnip_ui_input *in);

/* Which of the four directions do anything from where the ring is, as
 * CATNIP_HINT_* bits (#80).
 *
 * Here, beside the pass that acts on them, and not in whatever draws them: it
 * asks catnip_ui_input_dir() the same four questions catnip_ui_input_run() asks
 * and lights a direction when the answer is anything but NOTHING. Two copies of
 * that would drift, and a drifted hint is a hint that lies - which is worse
 * than none, because the whole value of it is in the dimming. */
enum {
    CATNIP_HINT_UP = 1u << 0,
    CATNIP_HINT_DOWN = 1u << 1,
    CATNIP_HINT_LEFT = 1u << 2,
    CATNIP_HINT_RIGHT = 1u << 3,
};
unsigned catnip_ui_input_hint(catnip_rt *rt, catnip_handle focus);

/* Forget every contact in flight and drop the ring.
 *
 * Called when control is handed away - the diagnostic page takes the screen -
 * for the reason press_gesture.h gives: a switch still held here must not carry
 * its start time into whatever comes next, and a ring pointing at a torn-down
 * node must resettle from the top rather than at a node that is gone. */
void catnip_ui_input_reset(catnip_ui_input *in);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_UI_INPUT_CORE_H */
