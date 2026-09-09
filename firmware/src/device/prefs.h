/*
 * prefs.h - where the owner's settings are kept, and which copy wins (#67).
 *
 * There are two copies and they are not equals.
 *
 * **NVS** is the device's own. It is where a setting is written the moment the
 * owner changes one, and it is the only copy that exists on a device with no
 * card - which is most of them, most of the time. A settings page whose
 * changes did not survive a reboot without a card would read as the device
 * ignoring the owner.
 *
 * **The card**, `/sd/catnip/config.json`, is the copy a person can read, edit
 * in a text editor, and carry to another device. Every save writes it too when
 * a card is present, so it is never stale by more than one save.
 *
 * **On boot the card wins**, when it is there and it parses. That is a
 * deliberate departure from the stock firmware, which keeps settings in NVS
 * alone: hand-editing the file is a thing this platform has always let an owner
 * do - see catnip_config.h - and a file that is read only when the device feels
 * like it is worse than one that is never read at all. The cost is real and
 * worth naming: a card that has been in a drawer for a month will, on the
 * boot after it is inserted, put its month-old settings back. What guards
 * against the worst version of that is that only a file which actually parses
 * counts - no file, an unreadable file, or one that is not JSON leaves NVS
 * exactly as it was, so an empty card cannot silently reset a device.
 *
 * The stored copy carries a schema version. A version this firmware does not
 * know is ignored rather than guessed at, which is what keeps a settings key
 * that changed meaning between firmwares from being read as the thing it used
 * to be.
 */
#ifndef CATNIP_PREFS_H
#define CATNIP_PREFS_H

#include "../catnip_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fill `cfg` with the settings this device should boot with: the built-in
 * defaults, then NVS on top, then the card's file on top of that. Never fails -
 * every layer is optional and a missing one leaves what was underneath. */
void catnip_prefs_load(catnip_config *cfg);

/* Write `cfg` to NVS, and to the card as well when one is mounted. Called when
 * the owner leaves the settings page having changed something, rather than on
 * every step: flash wears, an SD write is slow enough to be seen, and nobody
 * needs the eleven brightnesses passed through on the way to the twelfth. */
void catnip_prefs_save(const catnip_config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_PREFS_H */
