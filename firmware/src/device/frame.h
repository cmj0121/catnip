/*
 * frame.h - the app frame: the one bar every screen is drawn inside (#33).
 *
 * The chrome is the platform's, not each app's. An app declares a title and
 * builds a content tree; the shell composes battery, name and focus counter
 * around whatever screen is visible. That is not tidiness - consistency is a
 * property of having one drawer, and the two apps that shipped before this had
 * already drifted into two different status lines, one of them with no battery
 * on it at all.
 *
 * It also could not be done in an app even if an app wanted to. The top bar is
 * a *horizontal* arrangement, and the node model has no horizontal container by
 * deliberate choice; an app that could place the battery could place it in the
 * wrong corner.
 *
 * WHERE IT IS DRAWN. On lv_layer_top(), not on a screen. LVGL shows one screen
 * at a time and the app pushes and pops several, so a bar built into a screen
 * would be rebuilt on every push and would blink at exactly the moment a user
 * is watching. The top layer sits above every screen and outlives all of them,
 * which is what "the frame wraps the visible screen" means in LVGL's terms.
 * Screens are given a matching top padding so their content starts below it.
 */
#ifndef CATNIP_FRAME_H
#define CATNIP_FRAME_H

#include <stdbool.h>

#include "../catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The height the bar occupies, which is also the top padding a screen needs.
 * The app never sees this number; it is here so make_screen() and the bar agree
 * on one value rather than on two that happen to match. */
#define CATNIP_FRAME_BAR_H 26

/* Draw or hide the bar. Hidden is the honest state before anything has been
 * drawn and while the diagnostic page has the panel: a bar over a page that is
 * testing the panel would be the frame testing itself. */
void catnip_frame_show(bool on);

/* The app's name, drawn centred. "" leaves the header empty rather than showing
 * a placeholder, which is what the launcher wants - it is the frame, so naming
 * itself in its own bar would be an app introducing itself to nobody. */
void catnip_frame_set_title(const char *title);

/* The battery, top-left. Below zero means "not measured", and the frame then
 * draws no percentage at all rather than a plausible wrong one - the same rule
 * device.battery() answers -1 by. */
void catnip_frame_set_battery(int percent);

/* One pass of the bar against what is on screen: reads the counter the renderer
 * derives for the list being navigated and draws `N/total`, or blanks that
 * region when there is no list to count. Call it once per loop, after the
 * render pass, so it reads the tree as it was just drawn. */
void catnip_frame_step(catnip_rt *rt);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_FRAME_H */
