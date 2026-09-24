/*
 * usb_flash.h - the BOOT+RESET dance in software (Batch 3, #61).
 *
 * The shipping firmware is a TinyUSB composite (USB_MODE=0). That is what lets
 * the CDC console, the HID keyboard and the Mass Storage drive share one USB
 * device - and it is also what gives up the ROM's USB-Serial/JTAG
 * reset-to-download path, so every flash otherwise needs the manual dance (hold
 * BOOT, tap RESET, release BOOT). See docs/INSTALL.md.
 *
 * catnip_usb_flash_mode() reboots the chip into the ROM serial bootloader over
 * the same USB cable, so the next esptool run connects with no buttons pressed.
 * It is the one call the Flash Mode app makes; the app shows its warning and
 * takes its confirmation first, because this does not return - the chip comes
 * back up in the bootloader, and only a flash or a USB replug brings the
 * firmware back (the power button is answered by the firmware, so it does
 * nothing there).
 *
 * On any build that is not the composite - the poc/probe sketches, and the host
 * tests - it is a stub that does nothing, so this file compiles everywhere and
 * the symbol the HAL wires is always present.
 */
#ifndef CATNIP_USB_FLASH_H
#define CATNIP_USB_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Reboot into ROM download mode. Does not return on the device. */
void catnip_usb_flash_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_USB_FLASH_H */
