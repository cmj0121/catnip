/*
 * catnip_render.h - walk the ui.* tree and drive a rendering backend (#30).
 *
 * The renderer reads the retained widget tree that `ui.screen{...}` built and
 * calls a backend for each node. The device provides an LVGL backend (creates
 * real widgets); tests provide a recording backend. Keeping the traversal here,
 * behind a small vtable, means the on-device binding is a thin shim and the
 * traversal itself is verified on the host.
 */
#ifndef CATNIP_RENDER_H
#define CATNIP_RENDER_H

#include "catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Every string a callback is handed is owned by Lua and borrowed for the length
 * of that one call. The renderer guarantees it that far by keeping the Lua value
 * anchored on the Lua stack across the call; it drops the anchor as soon as the
 * call returns, and Lua is then free to collect the string. A backend that wants
 * a string to outlive its callback must copy it - the device's LVGL backend does
 * not have to think about this, because lv_label_set_text copies. Anyone adding
 * a field here follows the same rule: read it into the node's stack frame in
 * catnip_render.c, never into a pointer that outlives the frame. */
typedef struct {
    void *ud;
    void (*begin_screen)(void *ud);
    void (*label)(void *ud, const char *id, const char *text);
    void (*button)(void *ud, const char *id, const char *text);
    void (*end_screen)(void *ud);
} catnip_render_backend;

/* Render the current ui.root() tree via `be`. Returns the number of widgets
 * emitted, or -1 if there is no screen to render. */
int catnip_render(catnip_rt *rt, const catnip_render_backend *be);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_RENDER_H */
