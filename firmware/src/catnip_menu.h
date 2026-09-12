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
#include <stddef.h>
#include <stdint.h>

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

/* The text a glance cell of `type` shows for the given live values, written
 * into `out` (always NUL-terminated when `cap` > 0).
 *
 * Returns true when `type` is a glance this firmware knows how to fill - "time"
 * from `epoch`, "battery" from the charge - and false for any other string,
 * which is how an app built against a later firmware's glance degrades to an
 * ordinary icon cell here rather than to a blank one. A zero epoch and a
 * negative battery are "not known yet" and give the placeholder the cell opens
 * on (--:--, --%), the same admission the clock's and the battery's own faces
 * make.
 *
 * A plain switch on the type string on purpose: adding a glance is adding a
 * case here, and the ring, the ordering next to home and the update loop
 * already work for whatever the case fills in - none of them is a second
 * hardcoded clock. */
bool catnip_glance_text(const char *type, uint32_t epoch, int battery, char *out,
                        size_t cap);

/* Refresh every glance cell from its own type: the clock cell from `epoch`, the
 * battery cell from `battery`, each through catnip_glance_text. A negative
 * battery is "unknown", the value device.battery() and the header's gauge both
 * pass through unchanged.
 *
 * Writes only what differs, so the passes where nothing has changed cost
 * nothing at all - and a glance moves once a minute or slower, so that is most
 * of them. Replaces the clock-only setter: a glance is per app now, not one
 * time string written to all of them. */
void catnip_menu_update_glances(catnip_menu *m, uint32_t epoch, int battery);

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
