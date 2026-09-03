/*
 * sd_mount.cpp - STATUS: scaffold, UNTESTED (needs PlatformIO + hardware).
 * Issue #32: mount the microSD (1-bit SDMMC) via VFS at /sd so the shell and
 * fs.* can read it.
 */
#ifdef CATNIP_DEVICE_WIP
#include <Arduino.h>
// #include "esp_vfs_fat.h" / SD_MMC

// Returns true on success; the shell's apps root is /sd/catnip/apps and the
// fs.* base is set from here.
bool catnip_sd_mount()
{
    // TODO: SD_MMC.begin("/sd", true /*1-bit*/) and check the card.
    return false;
}
#endif /* CATNIP_DEVICE_WIP */
