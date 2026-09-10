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

#include "../catnip_render.h"
#include "../catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The height the bar occupies, which is also the top padding a screen needs.
 * The app never sees this number; it is here so make_screen() and the bar agree
 * on one value rather than on two that happen to match. */
#define CATNIP_FRAME_BAR_H 26

/* And the corner the control hint occupies, bottom-left, for the same reason
 * the bar's height is here: it is drawn on the top layer over every screen, so
 * whatever lays a screen out has to know what is already there. A screen that
 * ignored it would put its own bottom-left content under four arrows. */
#define CATNIP_FRAME_HINT_W 34
#define CATNIP_FRAME_HINT_H 30

/* Draw or hide the bar. Hidden is the honest state before anything has been
 * drawn and while the diagnostic page has the panel: a bar over a page that is
 * testing the panel would be the frame testing itself. */
void catnip_frame_show(bool on);

/* The app's name, drawn centred. "" leaves the header empty rather than showing
 * a placeholder, which is what the launcher wants - it is the frame, so naming
 * itself in its own bar would be an app introducing itself to nobody. */
void catnip_frame_set_title(const char *title);

/* Which of the four directions do something where the user is, as
 * CATNIP_HINT_* bits from ui_input_core.h. Drawn as a small cross in the
 * bottom-left corner, each arrow lit or dimmed.
 *
 * The dimming is the whole point. Several directions do nothing depending on
 * where you are - up on the cat has no grid to open yet, and a page of two
 * buttons has nothing for up and down to move - and on a device with no hint at
 * all, a direction that does nothing is indistinguishable from a device that
 * has stopped listening.
 *
 * A and B are deliberately not on it. They are the two gestures that mean the
 * same thing everywhere, and a hint is for what changes. */
void catnip_frame_set_hint(unsigned mask);

/* Draw the hint at all. Off while the diagnostic has the panel, over an app
 * that asked for the whole surface with `frame: "bare"`, and over one that said
 * `"hints": false` - which is a different claim from `bare` and worth making on
 * its own: this app's directions need no explaining.
 *
 * Separate from catnip_frame_show() because the bar and the hint are hidden for
 * different reasons and by different callers; they were one call, and the app
 * that wanted its own hint suppressed would have lost its title bar with it. */
void catnip_frame_show_hint(bool on);

/* The status strip beside the battery (#83): a glyph for a card in the slot, a
 * glyph for the radio being up, and the sync glyph while a clock sync is in
 * flight. Each appears only when true - absent, not dimmed, because a card that
 * is not there is nothing to show, unlike a direction the hint must dim.
 *
 * Pushed rather than polled: each subsystem says when its answer changes, so
 * the bar is not going to five drivers on every full-screen frame to draw two
 * glyphs. */
void catnip_frame_set_status(bool card, bool radio, bool syncing);

/* The battery, top-left. Below zero means "not measured", and the frame then
 * draws no percentage at all rather than a plausible wrong one - the same rule
 * device.battery() answers -1 by. */
void catnip_frame_set_battery(int percent);

/* The action bar (long A), drawn rising from the bottom over whatever is on
 * screen - the content stays visible underneath, because an action is always
 * about something the user can still see.
 *
 * Up to three cells, each `[icon] [name]`. `focus` is ringed when `modal` -
 * when the bar steps rather than binding. Otherwise nothing is ringed: the left
 * one *is* A and the right one *is* B, so a ring would point at a button that is
 * already under a thumb.
 *
 * `modal` is told rather than worked out from `n`. It used to be re-derived
 * here as `n >= 3`, and that is only one of the two ways a bar can be modal -
 * a pair whose second action is destructive steps too, because B will not carry
 * it. The File Browser offers exactly that on a folder, so long A there gave a
 * cursor that stepped and was never drawn.
 *
 * Drawn here rather than by an app for the reason the top bar is: consistency
 * is a property of having one drawer, and the one thing a user must be able to
 * carry between apps is what the dangerous gesture looks like.
 *
 * `names`/`icons` are borrowed for the length of the call. `n` of 0 puts it
 * away. */
void catnip_frame_set_actions(const char *const *names, const catnip_icon *icons, int n,
                              int focus, bool modal);

/* Which cell a tap at (x, y) landed on, or -1. The bar is the one piece of
 * chrome a finger may press: it is the finger's only route to Cancel, since
 * back and home have no touch. */
int catnip_frame_action_at(int x, int y);

/* How tall the action bar is when it is up - what the control hint is lifted by
 * so it sits on the bar's shoulder rather than under it. */
#define CATNIP_FRAME_ACT_H 44

/* The busy ring, and what it is waiting for.
 *
 * `what` is a short word drawn under it - "scanning", "loading" - or NULL to
 * put it away. One of these in the whole device, drawn by the platform, because
 * "working" is a sentence a user must be able to read the same way everywhere.
 *
 * It takes the whole panel, and takes it from the bar and the hint as well.
 * While it is up, nothing on the screen underneath is true any more - the app
 * being loaded is not the menu behind it - and a bar naming a screen that is
 * going away is a lie in a corner. An empty ground with one thing on it is the
 * honest picture of a device that has nothing to show yet.
 *
 * Call it every pass while busy - it turns itself from the clock, so the ring
 * runs at the same rate whatever else the loop is doing. */
void catnip_frame_set_busy(const char *what);

/* One pass of the bar against what is on screen: reads the counter the renderer
 * derives for the list being navigated and draws `N/total`, or blanks that
 * region when there is no list to count. Call it once per loop, after the
 * render pass, so it reads the tree as it was just drawn. */
void catnip_frame_step(catnip_rt *rt);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_FRAME_H */
