/*
 * shell_ui.cpp - STATUS: scaffold, UNTESTED (needs PlatformIO + hardware).
 * Issue #33: draw the shell menu + status bar on the display.
 * Issue #34 (render half): show each app's manifest icon (decode the PNG, e.g.
 * with a vendored lodepng, into an lv_img). The icon path is already carried in
 * catnip_app_entry.icon.
 */
#ifdef CATNIP_DEVICE_WIP
#include <lvgl.h>

extern "C" {
#include "../catnip_loader.h"
}

// Build a navigable menu of the discovered apps with name + 70x70 icon, plus a
// status bar (battery/wifi/time from the HAL). Selecting an entry launches it
// via the shell; returning shows the menu again.
void catnip_shell_ui_render(const catnip_app_entry *apps, int n, int selected)
{
    // TODO: draw with LVGL; decode apps[i].icon (PNG) -> lv_img; fall back to a
    // default icon when absent/undecodable.
    (void)apps; (void)n; (void)selected;
}
#endif /* CATNIP_DEVICE_WIP */
