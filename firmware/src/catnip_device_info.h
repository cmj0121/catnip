/*
 * catnip_device_info.h - the page that says what this device is (#69).
 *
 * One step down from the home ring, and the hub for everything below it: the
 * facts about the device, and under them the two places you would go having
 * read them - the preference page and the input diagnostic. Down used to open
 * the preference page directly and this page had no way in at all; putting the
 * facts first is the order the questions come in, because "what is this thing"
 * is answered before "change it" and long before "is the joystick broken".
 *
 * The rows are handed in as finished strings. That is the whole design: what a
 * chip is called, how much PSRAM is free and whether a card is in the slot are
 * all questions only the device can answer, and none of them belong in a module
 * that has to build a Lua tree. This side owns the tree; the platform owns the
 * facts, and hands them over already written out.
 *
 * Neither button is acted on where it is pressed. Both are latched, for the
 * same reason the launcher latches a pick: what happens next tears down the
 * page the handler is running on.
 */
#ifndef CATNIP_DEVICE_INFO_H
#define CATNIP_DEVICE_INFO_H

#include <stdbool.h>

#include "catnip_bar.h"
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
 * This page is the hub under home, and it is a page to be *read*: the facts
 * about the device, one line each, as one column of prose that scrolls. It used
 * to carry two tiles under the facts, and they were the thing this page was not
 * supposed to be - operations drawn on a screen. They are behind A now, where
 * every other operation in the device is, and the page is what it says it is.
 *
 * No selection is drawn on it either. A ring round "free heap" would promise
 * that pressing A there did something to the free heap; what the cursor is for
 * here is the reading position, and the scroll is the half of it that shows. */
void catnip_device_info_show(catnip_device_info *d, const char *const *rows, int n);

/* Which of the three was chosen since this was last asked, or
 * CATNIP_INFO_NONE. Reading clears it, so one press is acted on once. */
enum {
    CATNIP_INFO_NONE = 0,
    CATNIP_INFO_PREF,
    CATNIP_INFO_SIZES,
    CATNIP_INFO_DIAG,
};

/* The three, as the platform's own action catalogue. This page has no manifest
 * to declare them in - it is not an app - so it declares them here, and the bar
 * that draws them cannot tell the difference. */
const catnip_action *catnip_device_info_actions(int *n);
/* Rewrite the fact lines in place, keeping the page - the ring's position, the
 * list's scroll, everything the retained renderer is for.
 *
 * The facts go stale while they are being read: a card comes out, the battery
 * moves. A page that answered correctly on the way in and then quietly stopped
 * is the failure this page exists to avoid, so whoever shows it is expected to
 * keep asking. A row count different from the page's is ignored - that is a
 * different page, and the caller should show() it instead. */
void catnip_device_info_update(catnip_device_info *d, const char *const *rows, int n);

int catnip_device_info_take_action(catnip_device_info *d);

void catnip_device_info_free(catnip_device_info *d);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_DEVICE_INFO_H */
