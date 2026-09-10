/*
 * catnip_pins.h - which apps are kept off the carousel (#71).
 *
 * The set of unpinned app ids is a comma-separated string in the config, so it
 * survives a reboot and rides to the card like every other setting. The two
 * things done to it - "is this app unpinned" and "toggle this app" - are string
 * work with edges that are easy to get wrong (an id that is a prefix of
 * another, a trailing comma, a set that just emptied), so they live here in
 * plain C where a host test can pin them.
 */
#ifndef CATNIP_PINS_H
#define CATNIP_PINS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether `id` is in the comma-separated `csv` - i.e. whether the app is
 * unpinned, and so shown only in the grid rather than on the ring. Matches a
 * whole field, so "clock" does not match "clockwork". */
bool catnip_pins_contains(const char *csv, const char *id);

/* Add `id` to `csv` if absent, remove it if present; the toggle behind long-A
 * on a grid cell. `cap` is the buffer size including the null. A toggle that
 * would overflow the buffer is dropped rather than truncating the list into a
 * different set. Returns true if the set changed. */
bool catnip_pins_toggle(char *csv, size_t cap, const char *id);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_PINS_H */
