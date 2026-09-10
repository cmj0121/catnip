/*
 * catnip_ui.h - the ui.* component framework (issue #2).
 *
 * Apps describe screens with a small, stable set of high-level components
 * (ui.label, ui.button, ui.list, ui.screen) using a retained builder + callback
 * model, instead of touching LVGL directly. The result is a widget tree of
 * plain Lua nodes: the renderer walks it (LVGL on the device) and input is
 * delivered by firing a node's handler. Keeping the contract at this level lets
 * a firmware/LVGL upgrade happen without breaking existing apps.
 *
 * This installs the `ui` table (also as catnip.ui). The on-device LVGL renderer
 * and the optional unstable lvgl.* escape hatch are separate, later work.
 */
#ifndef CATNIP_UI_H
#define CATNIP_UI_H

#include <stdbool.h>
#include "catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load the ui module into the runtime. Returns 0 on success, non-zero on error
 * (reported through the runtime log). */
int catnip_ui_open(catnip_rt *rt);

/* True when an app has a screen up - see catnip_ui.c. The shell uses it to tell
 * a resident event-driven app from a script that has finished. */
bool catnip_ui_has_screen(catnip_rt *rt);

/* The title the app set with ui.title(s), or NULL when it never called it and
 * the manifest's name still stands. The returned string is valid until the next
 * call - copy it into whatever draws it.
 *
 * A title an app *changes* rather than one it has to declare: the File Browser
 * shows the directory it is in, which is not a fact the manifest can hold. */
const char *catnip_ui_title(catnip_rt *rt);

/* How many screens are stacked: 0 for an app that has drawn nothing, 1 for one
 * showing its root, more for each ui.push above it. It is what tells the
 * platform whether a short B has a screen to pop or has reached the app's root
 * and should leave. */
int catnip_ui_depth(catnip_rt *rt);

/* Which frame the *visible* screen asked for: 1 for bare, 0 for standard, and
 * -1 when it said nothing and the manifest's answer still stands.
 *
 * `frame` began as a manifest key, which made it a claim about the app rather
 * than about one of its screens - and the clock is the app that shows why that
 * is the wrong altitude. Its face is a canvas that wants the whole panel and
 * its setter is a page of columns that wants the bar back, so an app-wide flag
 * can only be wrong about one of them. A screen is what has a frame around it,
 * so a screen is what gets to say.
 *
 * The manifest key stays, and stays the default: an app whose screens are all
 * one shape still says it once. */
int catnip_ui_screen_frame(catnip_rt *rt);

/* The same question, already answered: whether the panel belongs to the app
 * right now, given what the manifest said.
 *
 * The two-line rule - the screen first, the manifest when the screen said
 * nothing - lives here rather than at the board, because the board is the one
 * layer no host test can reach. A rule that only exists in main.cpp is a rule
 * that is checked by flashing. */
bool catnip_ui_bare(catnip_rt *rt, bool manifest_bare);

/* Discard the visible screen, revealing the one beneath. Returns true if there
 * was one. This is ui.pop() called from the platform rather than by the app:
 * the app declined the back, so the pop is the platform's default action and
 * not something the app asked for. */
bool catnip_ui_pop(catnip_rt *rt);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_UI_H */
