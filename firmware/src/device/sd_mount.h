/*
 * sd_mount.h - the microSD card, mounted as a filesystem (#32).
 *
 * The card is where everything the owner controls lives: apps, the config
 * file, boot artwork. None of it is required - the device boots and runs
 * without a card - so mounting reports what it found rather than failing.
 */
#ifndef CATNIP_SD_MOUNT_H
#define CATNIP_SD_MOUNT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mount the card at /sd. Returns false when there is no card, which is a
 * normal state and not an error. Logs the card it found. */
bool catnip_sd_mount(void);

/* True once the card is mounted. */
bool catnip_sd_mounted(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_SD_MOUNT_H */
