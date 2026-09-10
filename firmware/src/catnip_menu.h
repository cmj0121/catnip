/*
 * catnip_menu.h - the shell's launcher menu, drawn through the renderer (#33).
 *
 * The menu is not special: it is a ui.* screen like any app builds - a list of
 * the installed apps with a status line above it - so the same renderer, the
 * same input layer and the same event queue that run an app also run the menu.
 * That is the point of it being here rather than a bespoke draw routine: the
 * menu is the first thing that exercises the whole path end to end, and it can
 * only do that by going through it.
 *
 * The controller stays in catnip_shell (launch, run, return); this owns only
 * the menu's own screen and the one fact the shell needs back from it - which
 * app the user picked. Selecting a row does not launch from inside the handler,
 * because launching tears the menu's own tree down and the handler is running
 * on a coroutine rooted in that tree. The pick is latched instead and the main
 * loop consumes it at a safe point, which is what catnip_menu_take_pick is for.
 */
#ifndef CATNIP_MENU_H
#define CATNIP_MENU_H

#include <stdbool.h>

#include "catnip_loader.h"
#include "catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct catnip_menu catnip_menu;

/* Create the menu over `rt`. The ui module must already be open (the shell
 * opens it). Returns NULL on failure. */
catnip_menu *catnip_menu_new(catnip_rt *rt);

/* Build - or rebuild - the menu screen listing `apps[0..n)`. Call it when
 * control first reaches the menu and again every time an app returns to it,
 * because the renderer's teardown between apps has cleared the tree by then.
 * The rows show each app's `name`; a pick is reported by its `id`. */
/* `fs_ready` says whether the device has storage right now. An app that asked
 * for the filesystem and cannot have it is shown dimmed and refuses to launch:
 * fs.* raises rather than answering "empty" to a device with no card, so
 * starting such an app would only fault it back to this screen, which reads as
 * the device ignoring the press. */
/* `unpinned` is the comma-separated set of app ids kept off the ring (#71): an
 * app appears on the carousel unless its id is in it. NULL or "" shows every
 * app, which is the default a fresh device has. */
void catnip_menu_show(catnip_menu *m, const catnip_app_entry *apps, int n, bool fs_ready,
                      const char *unpinned);

/* The id of the app the user activated since the last call, or NULL when none.
 * Reading it clears the latch, so it reports a pick to exactly one caller. The
 * returned pointer is owned by the menu and stays valid until the next
 * catnip_menu_show. */
const char *catnip_menu_take_pick(catnip_menu *m);

/* The name of the app the carousel is showing, or "" on home. The frame's
 * header reads it: a carousel cell is a picture and nothing else, so the only
 * place its name can be is the bar. Unlike the pick, reading this does not
 * clear it - it is a state, not an event. */
const char *catnip_menu_focus_name(const catnip_menu *m);

/* A value cell is a small face: the time in the middle, the date and the
 * weekday on the line under it, one at each end. Three strings because they are
 * three roles, and a node carries one.
 *
 * Writes only what differs, so the twenty-nine passes a minute where nothing
 * has changed cost nothing at all. */
void catnip_menu_set_glance(catnip_menu *m, const char *time);

/* Forget where the ring was, so the next rebuild opens on the cat.
 *
 * `catnip_menu_show` otherwise opens on the app that was last launched from it,
 * which is what makes the spec's promise true - short B returns you to where
 * you came from, and the launcher used to be the one exception. Long B is home
 * and says so by calling this. */
void catnip_menu_home(catnip_menu *m);

void catnip_menu_free(catnip_menu *m);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_MENU_H */
