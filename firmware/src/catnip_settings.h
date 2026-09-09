/*
 * catnip_settings.h - the preferences page, drawn through the renderer (#67).
 *
 * Down from the home section opens this. Like the launcher menu it is not a
 * special screen: it is a ui.* tree of the same kind an app builds, so the same
 * renderer, the same input layer and the same event queue run it.
 *
 * Every setting here is a **ladder of a few named steps**, not a continuous
 * range, because activating a column is what changes it: short A means "the
 * next value" and a next value needs a finite list to come from. Five steps is
 * enough to be worth having and few enough that the far end is four presses
 * away rather than eighty.
 *
 * Nothing here writes anything down. It reports the settings as they now stand
 * and whether they have changed since it was shown; where they are kept, and
 * when, belongs to the platform - see catnip_prefs.h on the device. That split
 * is what lets this file be tested on a host with no flash and no card.
 */
#ifndef CATNIP_SETTINGS_H
#define CATNIP_SETTINGS_H

#include <stdbool.h>

#include "catnip_config.h"
#include "catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct catnip_settings catnip_settings;

/* Create the page over `rt`. The ui module must already be open (the shell
 * opens it). Returns NULL on failure. */
catnip_settings *catnip_settings_new(catnip_rt *rt);

/* Build - or rebuild - the page, with the columns standing where `cfg` says.
 *
 * A setting whose stored value is not on the ladder (a hand-edited config file,
 * or a ladder that changed between firmwares) snaps to the nearest step rather
 * than being refused. The alternative is a column that shows one thing and
 * means another until it is next pressed. */
void catnip_settings_show(catnip_settings *s, const catnip_config *cfg);

/* The settings as they now stand, including anything stepped since the page was
 * shown. Owned by the page; valid until the next catnip_settings_show. */
const catnip_config *catnip_settings_config(const catnip_settings *s);

/* Whether anything has been stepped since this was last asked. Reading clears
 * it, so a caller that saves on the way out saves once, and a page nobody
 * touched costs no flash write at all. */
bool catnip_settings_take_dirty(catnip_settings *s);

void catnip_settings_free(catnip_settings *s);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_SETTINGS_H */
