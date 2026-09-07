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

/* Where the card appears. Everything under it is reached with ordinary POSIX
 * calls - fopen("/sd/x") opens a file on the card - because the SD driver
 * registers itself with the virtual filesystem under this name. That is what
 * lets the fs.* API in catnip_api.c, which is written against plain stdio and
 * knows nothing about SD_MMC, work on this board.
 *
 * Named here rather than written out twice: the mount call and whoever hands
 * this root to an app have to agree on it, and two string literals in two files
 * do not have to agree on anything. No trailing separator, so a caller joining
 * "%s/%s" onto it gets exactly one. */
#define CATNIP_SD_MOUNT_POINT "/sd"

/* Mount the card at CATNIP_SD_MOUNT_POINT. Returns false when there is no card,
 * which is a normal state and not an error. Logs the card it found. */
bool catnip_sd_mount(void);

/* True once the card is mounted. */
bool catnip_sd_mounted(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_SD_MOUNT_H */
