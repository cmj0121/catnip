/*
 * catnip_app_grid.h - every app at once, and pinning (#71).
 *
 * The carousel is the shortlist: the apps kept close, one at a time. This is
 * the rest - up from the ring opens a grid of every app there is, where one is
 * launched, and where long-A pins or unpins it so it comes or goes from the
 * ring. A carousel is a shortlist and stops working the moment it is used as a
 * directory; this is the directory.
 *
 * Touch can look but not act. A finger scrolls and selects, and that is all:
 * launching and pinning are the joystick's, because a grid is where a stray
 * tap would do the most damage - start the wrong app, unpin the one you rely
 * on - and the guard against that is that touch simply cannot. (#72 is the
 * general guard; this is the specific one the grid needs now.)
 *
 * Built through the renderer like the menu, and driven the same way: it latches
 * a pick and a pin, and the shell above launches and the config above stores.
 */
#ifndef CATNIP_APP_GRID_H
#define CATNIP_APP_GRID_H

#include <stdbool.h>

#include "catnip_runtime.h"
#include "catnip_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct catnip_app_grid catnip_app_grid;

catnip_app_grid *catnip_app_grid_new(catnip_rt *rt);
void catnip_app_grid_free(catnip_app_grid *g);

/* Build the grid from `apps[0..n)`, marking each pinned or not from the
 * comma-separated `unpinned` set (an app is pinned unless its id is in it), and
 * greying the ones that cannot run right now. */
void catnip_app_grid_show(catnip_app_grid *g, const catnip_app_entry *apps, int n,
                          bool fs_ready, const char *unpinned);

/* The id of the app to launch since last asked, or NULL. A joystick A on a
 * ready app; never a tap. Reading clears it. */
const char *catnip_app_grid_take_pick(catnip_app_grid *g);

/* The id whose pinning to toggle since last asked, or NULL. A joystick long-A;
 * never a tap. Reading clears it. */
const char *catnip_app_grid_take_pin(catnip_app_grid *g);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_APP_GRID_H */
