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
 * a role is cheap and removing one is not, which is why five is the whole list. */
typedef enum {
    CATNIP_STYLE_BODY = 0, /* the fallback, and therefore value 0 */
    CATNIP_STYLE_TITLE,
    CATNIP_STYLE_CAPTION,
    CATNIP_STYLE_PRIMARY,
    CATNIP_STYLE_DANGER,
} catnip_style_role;

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
    int selected;     /* list only: the selected child, as a zero-based index to
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

/*
 * Queue `event` (a short name such as "click") for the node behind `h`. Safe to
 * call from inside an LVGL event callback: it validates the handle, copies the
 * name, and returns. It runs no Lua and cannot re-enter. Returns 0 if queued,
 * -1 for a stale handle, -2 if the queue is full.
 *
 * A full queue drops the *newest* and logs once per drain. Dropping the oldest
 * would reorder a sequence, and a "release" delivered without its "press" is
 * worse than a press that did not register.
 */
int catnip_render_post(catnip_rt *rt, catnip_handle h, const char *event);

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
 * it owns and must release with luaL_unref. Returns 0 if the handler ran.
 *
 * It is a function pointer because where a handler runs is staged: today the
 * device installs catnip_sched_dispatch(), which resumes it once on its own
 * coroutine with the watchdog armed - so a runaway handler is stopped, and a
 * handler that yields fails loudly instead of silently. Making it yieldable
 * needs the scheduler to grow a run queue, which is #9's contract rather than
 * this one's; when it does, one line of wiring changes and no contract does. */
typedef int (*catnip_render_dispatch_fn)(void *ud, catnip_rt *rt, int node_ref,
                                         const char *event);
void catnip_render_set_dispatch(catnip_rt *rt, catnip_render_dispatch_fn fn, void *ud);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_RENDER_H */
