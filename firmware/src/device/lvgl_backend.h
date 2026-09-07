/*
 * lvgl_backend.h - the LVGL half of the ui.* renderer (#30).
 *
 * catnip_render.c walks the retained widget tree and decides what changed; this
 * is what turns those decisions into widgets on the panel. The split is the
 * point: everything that can be wrong about the diff is host-tested against a
 * recording backend, and what is left here is one LVGL call per verb.
 *
 * IT TAKES THE PANEL AND KEEPS IT. The boot animation blits straight to the
 * glass and knows nothing about LVGL, so the two cannot both be painting. Once
 * an app has drawn a widget, LVGL owns the screen for the rest of the session
 * and catnip_lvgl_backend_active() is how main.cpp knows to stop the mascot -
 * the same handover the diagnostic page makes, for the same reason. What it
 * does not yet do is hand the panel back: an app that exits leaves a blank
 * screen, because there is no menu to return to until the shell UI (#33) draws
 * one.
 *
 * IT IS NOT THE DIAGNOSTIC PAGE'S RIVAL. diag.cpp draws on lv_screen_active()
 * and never gives it up, and loop() stops stepping the shell the moment the
 * page is up, so the two never run in the same pass. main.cpp tears an app's
 * tree down before the page starts; see enter_diag() there for why that
 * ordering is load-bearing.
 */
#ifndef CATNIP_LVGL_BACKEND_H
#define CATNIP_LVGL_BACKEND_H

#include <stdbool.h>

#include "../catnip_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The backend, bound to the runtime an input event is posted back to. Returns
 * the same static vtable every time, so calling it twice is free. `rt` must
 * outlive every widget the backend draws, which the shell's teardown
 * guarantees: catnip_render_reset() destroys them all before the runtime goes.
 */
const catnip_render_backend *catnip_lvgl_backend(catnip_rt *rt);

/* True once an app's widgets have reached the panel. Anything that draws
 * outside LVGL - the boot animation, a splash frame - has to stop when this
 * turns true. */
bool catnip_lvgl_backend_active(void);

/* Mark the screen dirty so the next catnip_lvgl_step() paints it again. For
 * when the panel was painted over from outside LVGL, which LVGL has no way to
 * know about: the power button blanking it is the case that exists today. */
void catnip_lvgl_backend_redraw(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_LVGL_BACKEND_H */
