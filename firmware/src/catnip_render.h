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
