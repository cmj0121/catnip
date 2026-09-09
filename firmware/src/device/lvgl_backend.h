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

/* The palette, for the two platform overlays that are drawn outside the node
 * tree - the frame's bar and the fault toast. Both had their own copies of
 * these values with a comment saying whose they really were, which is a theme
 * change that silently misses one surface. */
uint32_t catnip_color_bg(void);
uint32_t catnip_color_text(void);
uint32_t catnip_color_faint(void);
uint32_t catnip_color_danger(void);

/* Mark the screen dirty so the next catnip_lvgl_step() paints it again. For
 * when the panel was painted over from outside LVGL, which LVGL has no way to
 * know about: the power button blanking it is the case that exists today. */
void catnip_lvgl_backend_redraw(void);

/* Move the focus ring onto the object this backend drew for `h`, taking it off
 * whatever wore it before. Pass CATNIP_HANDLE_NONE to clear it. The input layer
 * (#31) needs to show which focusable the joystick is on, and the renderer hands
 * out focus in handles - but the object behind a handle is the backend's to
 * dereference and no one else's (see the map comment in catnip_render.h), so the
 * input layer names the handle and the ring is moved here, inside the code that
 * made the object. Called every pass; a handle that has not changed since the
 * last call costs nothing, so the caller need not track what it last focused. */
void catnip_lvgl_backend_focus(catnip_handle h);

/* Which handle the ring is on, or CATNIP_HANDLE_NONE. The frame asks, because
 * the counter follows the list the user is navigating and the cursor is what
 * says which that is. */
catnip_handle catnip_lvgl_backend_focused(void);

/* Which column of a visible mixer the point (x, y) is over, and how far up its
 * bar - 0 at the bottom block, 100 at the top.
 *
 * This is the one place the platform asks the backend where something is, and
 * it exists because a drag is the one gesture whose meaning is a position
 * rather than a direction: "put this setting where my finger is" cannot be
 * expressed as a step. Nothing else may grow this way - the rule that an app
 * names things and never places them is unaffected, because an app is not the
 * one asking.
 *
 * Returns 0 when the point is over no such column. */
/* The running app asked for the whole panel: no room reserved for the frame's
 * bar, and a screen that centres what it holds rather than stacking it from the
 * top. Set when an app is launched and cleared when it leaves, because it is a
 * property of what is running rather than of any node.
 *
 * Centring is part of what "bare" means and not a separate knob. A canvas is
 * what an app asks for when its content is one thing to be looked at rather
 * than a page to be read down, and there is nowhere else on such a screen for
 * that thing to sensibly go. Rule 1 is untouched: the app still named a label
 * and never said where. */
void catnip_lvgl_backend_set_bare(bool bare);

int catnip_lvgl_backend_mixer_at(int x, int y, catnip_handle *h, int *pct);

/* The same measurement against one column that has already been chosen, so a
 * drag keeps the column it started on however far sideways the finger wanders.
 * Returns -1 when `h` is not a mixer column that is currently drawn. */
int catnip_lvgl_backend_mixer_pct(catnip_handle h, int y);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_LVGL_BACKEND_H */
