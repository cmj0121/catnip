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

#include "catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load the ui module into the runtime. Returns 0 on success, non-zero on error
 * (reported through the runtime log). */
int catnip_ui_open(catnip_rt *rt);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_UI_H */
