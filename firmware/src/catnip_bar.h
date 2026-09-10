/*
 * catnip_bar.h - the action bar: what long A produces.
 *
 * No operation is ever on a screen. Not Delete, not Open, not Reset - no app
 * draws a button for a thing it can do, and neither does the platform. A screen
 * shows what *is*, and what can be *done* exists in exactly one place: behind a
 * long press on the item it acts on. This is that place.
 *
 * The bar is the platform's, and that is the whole point of it. An app that
 * drew its own menu would eventually draw a different Delete - a different
 * word, a different corner, a different number of presses to reach - and the
 * one thing a user must be able to carry between apps is what the dangerous
 * gesture looks like. So an app *answers which actions apply* and never builds
 * anything: `on_options` returns ids out of its manifest's catalogue, and this
 * decides what the two or three buttons mean.
 *
 * Two shapes, and the difference is not cosmetic:
 *
 *   Two actions   The left one *is* A and the right one *is* B. The bar is a
 *                 picture of the two buttons already under the user's thumbs,
 *                 not a third thing to aim at, so it takes no directions and
 *                 the content underneath keeps all four.
 *
 *   Three         A modal, because three does not map onto two buttons. It
 *                 takes left and right for itself while it is up; A runs the
 *                 focused one and B puts it away. Up and down still belong to
 *                 the content below, which stays visible.
 *
 * Portable, with no LVGL and no board in it: what the buttons mean is a
 * decision, and a decision that can only be exercised by flashing is a decision
 * nothing checks.
 */
#ifndef CATNIP_BAR_H
#define CATNIP_BAR_H

#include <stdbool.h>

#include "catnip_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/* More than this on one item is a menu pretending to be a bar. Three is already
 * the modal shape; eight is the point at which the answer is that the app has
 * too many verbs, not that the bar needs to scroll. */
#define CATNIP_BAR_MAX         CATNIP_ACTIONS_MAX
#define CATNIP_ACTION_NAME_MAX 24
#define CATNIP_ACTION_ICON_MAX 16

/* One entry of an app's catalogue, as the manifest declares it. */
typedef struct {
    char id[CATNIP_ACTION_ID_MAX];
    char name[CATNIP_ACTION_NAME_MAX];
    char icon[CATNIP_ACTION_ICON_MAX];
    /* Cannot be undone. The platform refuses to put one of these on B: B is the
     * escape everywhere else in the device, and an app that put Delete there
     * would misfire on exactly the reflex a user has when they want out. */
    bool destructive;
} catnip_action;

typedef struct {
    catnip_action items[CATNIP_BAR_MAX];
    int n;
    int focus; /* which one A runs, in the three-icon shape */
    bool up;
    /* Who asked, and about what: the list the long press landed on and the row
     * it was about. Both travel back out with the answer, because an action is
     * always about an item and the app has no other way to be told which. */
    catnip_handle owner;
    int index;
} catnip_bar;

/* Put a bar up over `owner`'s row `index` with these actions. Fewer than one is
 * not a bar and leaves it down; more than CATNIP_BAR_MAX is truncated. */
void catnip_bar_open(catnip_bar *b, const catnip_action *items, int n,
                     catnip_handle owner, int index);

void catnip_bar_close(catnip_bar *b);
bool catnip_bar_up(const catnip_bar *b);

/* Whether the bar has taken left and right for itself.
 *
 * A bar is modal exactly when it cannot be said as two buttons. Three cannot,
 * because there are only two buttons. And two cannot when the second of them is
 * something that cannot be undone: B is the escape everywhere in the device, so
 * the platform will not put Delete on it - and an action nothing can reach is
 * not an action, so the bar steps instead.
 *
 * That is the whole rule, and it is why the shapes are not a thing an app
 * chooses: what a bar looks like follows from what is on it. */
bool catnip_bar_modal(const catnip_bar *b);

/* Step the focus by `dir`, clamping. Does nothing unless the bar is modal - in
 * the two-icon shape there is nothing to step, because both are already bound
 * to a button. */
void catnip_bar_step(catnip_bar *b, int dir);

/* The action id A runs now, or NULL. Two icons: the left one. Three: the
 * focused one. The bar is left up either way - closing it is the caller's, once
 * it knows whether the action pushed something of its own. */
const char *catnip_bar_activate(const catnip_bar *b);

/* The action id B runs now, or NULL when B only puts the bar away.
 *
 * Two icons: the right one, unless the manifest marked it destructive - then
 * NULL, and B is the escape it is everywhere else. Three: always NULL, because
 * a modal's B is how you leave it. */
const char *catnip_bar_cancel(const catnip_bar *b);

/* The action id a finger ran by tapping cell `cell`, or NULL. A tap runs an
 * icon directly in both shapes: it is the finger's only route to Cancel, which
 * is the reason the bar carries a visible one at all. */
const char *catnip_bar_tap(const catnip_bar *b, int cell);

/* Take the ids the last `options` handler answered with, resolve them against
 * an app's catalogue, and put the bar up over `owner`'s row `index`. Returns
 * how many actions went on it - 0 when the app answered with nothing, which is
 * the ordinary case for a page that does its own thing behind long A.
 *
 * The resolution is the point of it: an app answers with names it declared, and
 * anything it did not declare is dropped rather than drawn, because a button
 * with nothing written on it is worse than one button fewer. */
int catnip_bar_offer(catnip_bar *b, catnip_rt *rt, const catnip_action *catalogue,
                     int n_catalogue, catnip_handle owner, int index);

/* Look `id` up in a catalogue. NULL when the app named an action it never
 * declared, which is an app bug and is dropped rather than drawn: a button with
 * no name on it is worse than one button fewer. */
const catnip_action *catnip_bar_find(const catnip_action *catalogue, int n,
                                     const char *id);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_BAR_H */
