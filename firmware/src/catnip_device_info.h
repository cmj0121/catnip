/*
 * catnip_device_info.h - the page that says what this device is (#69).
 *
 * Down from the preference page, when nothing on it is live. Rows of text and
 * nothing else: this page answers questions rather than offering choices, so it
 * needs no new shape and no new gesture.
 *
 * The rows are handed in as finished strings. That is the whole design: what a
 * chip is called, how much PSRAM is free and whether a card is in the slot are
 * all questions only the device can answer, and none of them belong in a module
 * that has to build a Lua tree. This side owns the tree; the platform owns the
 * facts, and hands them over already written out.
 *
 * The last row is not a fact. It enters the input diagnostic (#42), and it is
 * latched rather than acted on, for the same reason the launcher latches a
 * pick: the page that would be torn down is the one the handler is running on.
 */
#ifndef CATNIP_DEVICE_INFO_H
#define CATNIP_DEVICE_INFO_H

#include <stdbool.h>

#include "catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Enough for everything the page shows with room to add a line, and short
 * enough that the whole page is one allocation nobody has to think about. */
#define CATNIP_INFO_MAX_ROWS 12
#define CATNIP_INFO_ROW_MAX  64

typedef struct catnip_device_info catnip_device_info;

catnip_device_info *catnip_device_info_new(catnip_rt *rt);

/* Build - or rebuild - the page from `rows[0..n)`, each a finished line.
 *
 * `action` is the label for the last row, the one that does something rather
 * than says something, or NULL for a page that is only facts. It is drawn apart
 * from them so that a row which acts cannot be mistaken for a row which
 * reports. */
void catnip_device_info_show(catnip_device_info *d, const char *const *rows, int n,
                             const char *action);

/* Whether the action row has been activated since this was last asked. Reading
 * clears it, so one press is acted on once. */
bool catnip_device_info_take_action(catnip_device_info *d);

void catnip_device_info_free(catnip_device_info *d);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_DEVICE_INFO_H */
