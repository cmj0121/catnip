/*
 * usb_hid.h - the device as a USB keyboard, half of the composite image (Batch 1).
 *
 * The shipping firmware runs one USB image: a TinyUSB composite of the CDC
 * console (which is also the port esptool flashes over) and this HID keyboard
 * (USB_MODE=0, see platformio.ini). The keyboard interface is *always present* -
 * a host that enumerates the device sees a keyboard beside the serial port from
 * the first boot - but it is inert. No key report is ever sent until something
 * enables it, and nothing does until the owner turns it on in the HID app.
 *
 * That arm gate is the whole safety of this surface. HID is disabled at boot;
 * enable and disable are a runtime flag, not a re-enumeration, so turning
 * typing on and off costs no reboot and the host sees the keyboard either way.
 * True appear/disappear is a later refinement (PLAN #2).
 *
 * The device-specific USB is behind these four calls so the rest of the
 * firmware - and the Lua surface over it (catnip_hal.h, service.usb.*) - never
 * touches TinyUSB, and the host build stubs them to nothing.
 */
#ifndef CATNIP_USB_HID_H
#define CATNIP_USB_HID_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the keyboard class. The HID *interface* was already registered before
 * app_main by the global keyboard object's constructor - so it is part of the
 * composite the host first enumerates - and USB itself came up with the CDC
 * console on boot; this only starts the class and is idempotent. Call it once,
 * early in setup(). Typing stays disabled until catnip_usb_hid_enable(true). */
void catnip_usb_hid_begin(void);

/* Arm or disarm typing. Disarmed at boot; while disarmed catnip_usb_hid_key()
 * sends nothing, so a keyboard the host can see never types. */
void catnip_usb_hid_enable(bool on);

/* Whether typing is armed. False at boot and on any build without the composite
 * keyboard. */
bool catnip_usb_hid_enabled(void);

/* Tap one key: hold `mods` (a HID modifier bitmask), press `usage` (a HID usage
 * id, 0 for a modifier-only chord), then release. A no-op unless armed - the
 * gate is checked here as well as at the Lua boundary, so it holds on the metal
 * even for a caller that did not come through service.usb.*. */
void catnip_usb_hid_key(unsigned char mods, unsigned char usage);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_USB_HID_H */
