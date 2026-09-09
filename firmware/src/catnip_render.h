/*
 * catnip_render.h - walk the ui.* tree and drive a rendering backend (#30).
 *
 * The renderer reads the retained widget tree that `ui.screen{...}` built and
 * reconciles it against what a backend has already drawn. The device provides
 * an LVGL backend (real widgets); tests provide a recording backend. Keeping
 * the traversal and the diff here, behind a small vtable, means the on-device
 * binding is a thin shim and everything that can be wrong about the diff is
 * verified on the host.
 *
 * The model is *retained*, not rebuild-on-change: a node keeps the object the
 * backend made for it, a changed node updates that object in place, and only
 * structural change creates or destroys. That is what lets focus, scroll
 * position and an in-flight press survive an app that polls a value every five
 * seconds - the app the README advertises. It has a price, and it is the app
 * author who pays it: nodes are matched by *table identity*, so an app that
 * rebuilds its rows into fresh tables on every poll gets every row destroyed
 * and recreated, losing exactly what retention was for. Keeping scroll and
 * selection means keeping the node tables.
 */
#ifndef CATNIP_RENDER_H
#define CATNIP_RENDER_H

#include <stdint.h>

#include "catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A handle is (generation << 16) | index. The index names a slot in the
 * renderer's table; the generation is bumped every time that slot is freed, so
 * a handle that outlived its node resolves to nothing rather than to whatever
 * node moved into the slot afterwards. That case is not exotic: the backend
 * stores this integer on its object, and an input event for a deleted object
 * can already be in flight when the object goes away. Without the generation
 * the symptom is a press on A running B's handler, with no error and no log.
 *
 * The generation is fifteen bits, not sixteen, so that a handle is never
 * negative and CATNIP_HANDLE_NONE stays the only invalid value. It wraps after
 * 32768 reuses of the same slot index; a stale handle lives for at most one
 * frame, so nothing can survive that many recycles of one index and still be
 * around to collide.
 */
typedef int32_t catnip_handle;
#define CATNIP_HANDLE_NONE ((catnip_handle)(-1))

typedef enum {
    CATNIP_NODE_SCREEN,
    CATNIP_NODE_LABEL,
    CATNIP_NODE_BUTTON,
    CATNIP_NODE_LIST,
} catnip_node_kind;

/* A style *role*, not a style. What a role looks like - font, colour, weight -
 * is the backend's, because it lives with the fonts on the device. An unknown
 * name from Lua resolves to BODY rather than raising, so an app written against
 * a later firmware degrades on an older one instead of failing to open. Adding
 * a role is cheap and removing one is not, which is why the list is short and
 * every entry on it has to earn a difference somebody can see. */
typedef enum {
    CATNIP_STYLE_BODY = 0, /* the fallback, and therefore value 0 */
    CATNIP_STYLE_TITLE,
    CATNIP_STYLE_CAPTION,
    CATNIP_STYLE_PRIMARY,
    CATNIP_STYLE_DANGER,
    /* One number large enough to own the screen: a time, a level, a count.
     *
     * It is the same argument that made `title` a role at all - two roles set
     * in the same face at the same size are one role with two names, and an app
     * that marked its big number would have nothing to show for it. This one is
     * three times the size of anything else here, which is a difference nobody
     * can miss.
     *
     * It is for a *quantity*, not for large text. Nothing in the renderer can
     * enforce that; what keeps it honest is that there is no second place to
     * reach for - the five roles above are the only way to set words, and this
     * one is shaped like a readout. */
    CATNIP_STYLE_DISPLAY,
} catnip_style_role;

/* A glyph a row or button wears in front of its text. Like a style role it is
 * a *name from a fixed set*, resolved by the backend with the fonts, and an
 * unknown name from Lua degrades to NONE rather than raising - so an app
 * written against a later firmware loses its icon on an older one instead of
 * failing to open.
 *
 * It is an id and not a string on purpose. The set is small, bounded and the
 * platform's, so it needs no allocation, no lifetime discipline and no cache of
 * its own in the slot; the app names a *category* here, and the one image an
 * app supplies is its manifest identity icon, which is a different mechanism.
 */
typedef enum {
    /* Nothing to draw. An icon name this firmware does not know resolves here,
     * so an app written against a later one loses its icon rather than failing
     * to open - and it loses it silently, because a row that quietly has no
     * glyph is better than forty-five rows each wearing a "missing" box.
     *
     * That is not the same as CATNIP_ICON_PLACEHOLDER, which is for a slot that
     * must show something and has nothing to show. */
    CATNIP_ICON_NONE = 0,
    CATNIP_ICON_FOLDER,
    CATNIP_ICON_FILE,
    /* The slot is filled, but by nothing in particular: an app that shipped no
     * icon.png, an image that would not decode. It says "something belongs
     * here" where NONE says "nothing does". */
    CATNIP_ICON_PLACEHOLDER,
    CATNIP_ICON_IMAGE,
    CATNIP_ICON_AUDIO,
    CATNIP_ICON_SETTINGS,
    CATNIP_ICON_EDIT,
    CATNIP_ICON_TRASH,
    CATNIP_ICON_REFRESH,
    CATNIP_ICON_WARNING,
    CATNIP_ICON_OK,
    CATNIP_ICON_CLOSE,

    /* The mascot, and not one of the twelve drawn glyphs: it is a platform
     * image rather than a category, it is the only thing here that is a
     * picture of something, and it is drawn at whatever size the shape on
     * screen gives it rather than at 14 beside a word. It sits after CLOSE so
     * the twelve keep their codepoints. */
    CATNIP_ICON_MASCOT,
} catnip_icon;

/* How a list arranges its children. The node is the same either way - the same
 * `selected`, the same events, the same rows underneath - and only the flow
 * differs, which is why this is a property rather than a second node kind.
 *
 * ROWS is a column of `[icon] [name]` lines, the ordinary case. CAROUSEL shows
 * one child at a time, filling the region, and steps sideways: it is what the
 * home screen is, and what "one icon" in the spec's main region means. */
typedef enum {
    CATNIP_LAYOUT_ROWS = 0, /* the fallback, and therefore value 0 */
    CATNIP_LAYOUT_CAROUSEL,
    /* Columns of vertical bars, side by side - the shape a page of settings
     * wants, where every child is a value rather than a thing to choose. Height
     * carries the number, so the columns can be read against each other at a
     * glance, and the whole column is the touch target rather than a row of
     * text. See `value` below. */
    CATNIP_LAYOUT_MIXER,
    /* Children side by side, each keeping its own style role - a strip rather
     * than a list. It exists because a row of items that are not all the same
     * cannot be one label: a node carries one role, and "these seven letters,
     * with today's brighter than the rest" is seven nodes or it is nothing. */
    CATNIP_LAYOUT_ROW,
    /* A centrepiece with a line above it and a line below: what a bare screen
     * has always been laid out as, made available as a layout so that anything
     * can be one. A carousel cell that is a clock face is the case that asked
     * for it - the cell is the whole region, and a face is what belongs in a
     * whole region. */
    CATNIP_LAYOUT_CANVAS,
} catnip_node_layout;

enum {
    /* Set on the kinds that can take input - button and list - unless the node
     * is hidden or disabled. The backend builds its input group out of these
     * without the renderer needing to know what a group is. */
    CATNIP_NODE_FOCUSABLE = 1u << 0,
    CATNIP_NODE_HIDDEN = 1u << 1,
    CATNIP_NODE_DISABLED = 1u << 2,
};

/*
 * Everything a backend is allowed to know about a node. Flat, with no Lua in
 * it, so the shim under this vtable never links against the VM.
 *
 * Both strings are owned by Lua and borrowed for the length of the one call
 * they are passed to. The renderer guarantees that far by keeping the Lua value
 * anchored on the Lua stack across the call and dropping the anchor as soon as
 * it returns; Lua is then free to collect the string. A backend that wants a
 * string to outlive its callback must copy it - LVGL's lv_label_set_text does,
 * which is why the device backend does not have to think about it. Anyone
 * adding a field here follows the same rule: read it into the node's stack
 * frame in catnip_render.c, never into a pointer that outlives the frame.
 */
typedef struct {
    catnip_node_kind kind;
    catnip_style_role style;
    const char *id;   /* "" when the app named nothing. A name for logs, never
                       * a key: `id` is optional and nothing enforces that it is
                       * unique, so identity is the node table instead. */
    const char *text; /* "" when absent. */
    catnip_icon icon; /* the glyph in front of the text, or NONE */
    /* An image to draw instead of that glyph, named rather than pointed at:
     * "" for none. The platform decides what a name resolves to, the same way
     * it decides what `folder` looks like - so an app names its own identity
     * icon and never hands the renderer a pointer to pixels.
     *
     * Borrowed for the length of the call, like `text` and `id`. */
    const char *image;
    catnip_node_layout layout; /* list only: how its children are arranged */

    /* A quantity this node stands for, 0-100, or -1 when it does not stand for
     * one. `value` is the fill and `value_text` is what is printed above it -
     * two fields rather than one because the platform cannot derive the second
     * from the first: 70 reads as "70%" for a brightness and as "2.5 s" for a
     * period, and only the owner of the setting knows which.
     *
     * The percentage is the platform's unit deliberately. A bar has one height
     * and the frame has one way to draw it; letting each setting bring its own
     * range would make the drawing code carry arithmetic that belongs to
     * whoever chose the range.
     *
     * `value_text` is borrowed for the length of the call, like `text`. */
    int value;
    const char *value_text;
    /* How many rungs the ladder has, or 0 when the quantity is continuous.
     *
     * A discrete value is drawn as that many blocks rather than as one filled
     * bar, because those are two different promises: a bar says "anywhere along
     * here", and this value cannot go anywhere along here - it can only be one
     * of five things. Drawing it as a bar invites a drag that would have to be
     * quietly rounded, and a control that silently disagrees with the finger is
     * worse than one that never offered. */
    int steps;
    int selected; /* list only: the selected child, as a zero-based index to
                       * match `index` below, or -1 for none. Lua's `selected`
                       * prop is one-based like every other Lua index; the
                       * renderer converts, so a backend never has to. */
    unsigned flags;
} catnip_node_desc;

/*
 * The backend. Every callback may be NULL, which is how a test backend records
 * only the verbs it cares about.
 *
 * The backend owns the map from handle to whatever it drew - the renderer keeps
 * no pointer to it, because the only thing that can safely dereference an
 * object is the code that made it. Store the handle on the object too (LVGL:
 * lv_obj_set_user_data), because that is the road back from an input event to
 * catnip_render_post().
 */
typedef struct {
    void *ud;

    /* Every pass is bracketed, changed or not, so a backend can batch its work
     * and a recording backend can frame its transcript. A pass that changes
     * nothing is begin_pass immediately followed by end_pass. */
    void (*begin_pass)(void *ud);
    void (*end_pass)(void *ud);

    /* Create the object for `h` under `parent` (CATNIP_HANDLE_NONE for a
     * screen) at zero-based child position `index`.
     *
     * Returns 0 on success and non-zero on failure. It has a return because it
     * can genuinely fail: LVGL allocates and PSRAM runs out. With a void return
     * the renderer would keep a slot alive whose object is NULL, hand it
     * update() calls forever, and leave the screen quietly missing a widget -
     * a silently wrong screen, which is the failure mode this contract exists
     * to be free of. On failure the renderer frees the slot, so `h` must not be
     * used afterwards, and it will not retry this node until the tree changes
     * at that position or the screen is replaced. */
    int (*create)(void *ud, catnip_handle h, catnip_handle parent, int index,
                  const catnip_node_desc *d);

    /* Apply `d` to an existing object. Called only when the renderer's own
     * last-sent cache says something actually differs, so a backend must not
     * re-compare and an unchanged tree costs it nothing. */
    void (*update)(void *ud, catnip_handle h, const catnip_node_desc *d);

    /* This node kept its object but changed position among its parent's
     * children (LVGL: lv_obj_move_to_index). It exists because children are
     * matched by identity, so a node that moved is still the same node and
     * destroying its object would throw away what retention is for. */
    void (*move)(void *ud, catnip_handle h, int index);

    /* Destroy this object and nothing else. Children are destroyed by their own
     * calls, which arrive first, leaf to root.
     *
     * The backend must not lean on LVGL's cascade and must not defer with
     * lv_obj_delete_async(). The cascade is wrong because the backend would
     * still hold a map entry for every child whose object had just been freed
     * underneath it - and a recording backend produces a legal transcript
     * either way, so the tests would pass and the device would use freed
     * memory. The synchronous delete is affordable because a pass never runs
     * inside lv_timer_handler(); those two decisions hold each other up and
     * neither should be revisited alone. */
    void (*destroy)(void *ud, catnip_handle h);

    /* This screen is now the visible one. Separate from create() because
     * building a screen and showing it are two events, and because a pushed
     * screen shows one that was built several passes ago. */
    void (*show_screen)(void *ud, catnip_handle h);
} catnip_render_backend;

enum {
    CATNIP_RENDER_NO_SCREEN = -1, /* nothing to draw - the ordinary state before
                                   * an app has built a screen, not an error */
    CATNIP_RENDER_FAILED = -2,    /* at least one backend call failed this pass;
                                   * the screen is incomplete and it was logged */
};

/* One reconciliation pass over the visible screen. Returns the number of
 * structure calls made (create + update + move + destroy), so 0 means "nothing
 * changed" - a property worth asserting, because it is what keeps a polling app
 * from repainting the display twenty times a second. Call it from the main
 * loop, after the display's own step has returned. */
int catnip_render(catnip_rt *rt, const catnip_render_backend *be);

/* Destroy every live slot leaf to root, clear the renderer's tables and its
 * event queue, forget every screen, and clear the ui module's own roots so the
 * tree is finally unreachable.
 *
 * The owner is whoever owns the app's lifetime - the shell - and the moment is
 * when the scheduler reports CATNIP_DONE or CATNIP_ERROR, and again before the
 * next app starts. This is not tidiness: the Lua heap is accounted, so a leaked
 * screen tree is charged to whatever runs next, and the symptom of skipping it
 * is the *following* app failing to allocate. `be` may be NULL when nothing was
 * ever drawn; everything else is still released. */
void catnip_render_reset(catnip_rt *rt, const catnip_render_backend *be);

/* The live focusable handles in tree order, which is what a joystick's
 * next/previous walks. Returns how many there are, which may exceed `max`. */
int catnip_render_focus_order(catnip_rt *rt, catnip_handle *out, int max);

/* --- input --- */

/* The `index` of an event that names no child. It is what every event carried
 * before rows became touchable, and it is what reaches Lua as `nil`. */
#define CATNIP_INDEX_NONE (-1)

/*
 * Queue `event` (a short name such as "click") for the node behind `h`. Safe to
 * call from inside an LVGL event callback: it validates the handle, copies the
 * name, and returns. It runs no Lua and cannot re-enter. Returns 0 if queued,
 * -1 for a stale handle, -2 if the queue is full.
 *
 * `index` is the zero-based child this event is about, or CATNIP_INDEX_NONE.
 * It exists for one reason: a tap lands on a row, but a row is not focusable
 * and has no handlers, so the event goes to the row's *list* and the index is
 * the only way that list can know which row was touched. The joystick's own
 * prev/next/click name no child and pass the sentinel, which is why a handler
 * written before this existed still sees exactly what it did.
 *
 * A full queue drops the *newest* and logs once per drain. Dropping the oldest
 * would reorder a sequence, and a "release" delivered without its "press" is
 * worse than a press that did not register.
 */
int catnip_render_post(catnip_rt *rt, catnip_handle h, const char *event, int index);

/* The same, for the one event whose *answer* the platform reads back: `back`.
 * The handler's return value is latched for catnip_render_take_claim().
 *
 * Marking it at the post rather than by name in the drain keeps the renderer
 * from knowing what "back" is, and it is what stops an unrelated handler from
 * answering a question it was never asked - an on_click that happens to return
 * true would otherwise leave a claim standing for the next B to find. */
int catnip_render_post_claimable(catnip_rt *rt, catnip_handle h, const char *event,
                                 int index);

/*
 * Deliver everything queued, and return how many were delivered. Call it in the
 * main loop at a safe point - after the display step, before catnip_render():
 *
 *     catnip_lvgl_step();      // LVGL dispatches; posts land in the queue
 *     catnip_render_drain(rt); // handlers run here, and nowhere else
 *     catnip_sched_step(s);    // the app's own coroutine advances
 *     catnip_render(rt, be);   // the pass sees whatever they changed
 *
 * Handlers do not run during the input event, and that is deliberate. Running
 * them there would mean no watchdog (the count hook is armed on a coroutine, so
 * a handler on the main state could spin forever and freeze the device), no
 * yielding, and deleting an LVGL object while LVGL is dispatching that object's
 * event. Draining here dissolves all three rather than managing them.
 *
 * The consequence app authors have to be told: on_click is asynchronous with
 * respect to the press. It runs at most one frame later, and two presses in the
 * same frame both queue.
 */
int catnip_render_drain(catnip_rt *rt);

/* What actually runs a handler. It is handed a registry ref to the node, which
 * it owns and must release with luaL_unref. The handler is called as
 * `fn(node, index)`, so an app reads the node it was fired on as `self` and the
 * touched row as the second argument; a handler that declares neither is
 * unaffected, which is why adding both broke no app.
 *
 * It is a function pointer because where a handler runs is staged: today the
 * device installs catnip_sched_dispatch(), which resumes it once on its own
 * coroutine with the watchdog armed - so a runaway handler is stopped, and a
 * handler that yields fails loudly instead of silently. Making it yieldable
 * needs the scheduler to grow a run queue, which is #9's contract rather than
 * this one's; when it does, one line of wiring changes and no contract does.
 *
 * It reports three outcomes rather than two, because one event needs the
 * handler's answer and not merely the fact that it ran: 0 for a handler that
 * ran and returned falsy, 1 for one that ran and returned something truthy, and
 * negative for a handler that faulted or was not there at all. `back` is what
 * that distinction is for - see catnip_render_take_claim(). */
typedef int (*catnip_render_dispatch_fn)(void *ud, catnip_rt *rt, int node_ref,
                                         const char *event, int index);
void catnip_render_set_dispatch(catnip_rt *rt, catnip_render_dispatch_fn fn, void *ud);

/* The handle of the screen the backend was last told to show, or
 * CATNIP_HANDLE_NONE before anything was drawn. It is the node a screen-wide
 * event - `back` - is posted to, since the visible screen is the only thing the
 * platform can address without the app naming something. */
catnip_handle catnip_render_visible_screen(catnip_rt *rt);

/* The list behind `h`'s selected child, as a zero-based index, or
 * CATNIP_INDEX_NONE for a node that is not a list, has nothing selected, or is
 * gone. It reads the descriptor the renderer already caches, so it costs no
 * Lua: the platform needs it to say *which* item a long press was about, and
 * asking Lua from inside the input layer would run app code on the wrong side
 * of the drain. */
int catnip_render_selected(catnip_rt *rt, catnip_handle h);

/* How the list behind `h` arranges its children, or CATNIP_LAYOUT_ROWS for
 * anything that is not a list. The input layer asks, because a carousel is
 * stepped with left and right where a column is stepped with up and down -
 * the same `prev`/`next` either way, reached by whichever direction the shape
 * on screen makes obvious. */
catnip_node_layout catnip_render_layout(catnip_rt *rt, catnip_handle h);

/* The `N/total` the frame draws for the list the user is navigating: `n` is the
 * one-based selected row and `total` the row count. Returns 1 when there is a
 * counter to draw and 0 when there is not, in which case the frame leaves that
 * region blank rather than showing `0/0`.
 *
 * Which list: the focused one when `focus` names a list, else the only list on
 * the visible screen, else none. Stepping the focus onto a button therefore
 * keeps the counter on the list rather than blanking it, because the user has
 * not left the list - they are still looking at it.
 *
 * The frame derives this rather than the app supplying it. An API that set the
 * counter text would scatter the one-based conversion the renderer already does
 * once, and would let two apps disagree about what `total` counts. */
int catnip_render_counter(catnip_rt *rt, catnip_handle focus, int *n, int *total);

/* Whether the handler of a claimable post returned something truthy, cleared as
 * it is read.
 *
 * It exists so that `back` can be a negotiation without the platform having to
 * run a handler synchronously: B posts `back`, the drain runs on_back on the
 * watchdogged coroutine like every other handler, and the caller reads the
 * answer here afterwards - in the same pass, because the drain is upstream of
 * the shell in the loop. No claim (no handler, a fault, a falsy return) leaves
 * this 0, which is what makes "an app that wrote no on_back" the default rather
 * than a special case. */
int catnip_render_take_claim(catnip_rt *rt);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_RENDER_H */
