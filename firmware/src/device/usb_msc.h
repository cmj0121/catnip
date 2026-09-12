/*
 * usb_msc.h - the device as a USB drive, the third interface of the composite
 * image (Batch 2, #56).
 *
 * The shipping firmware is a TinyUSB composite: the CDC console, the HID
 * keyboard (usb_hid.h), and - added here - a Mass Storage drive that exposes
 * the whole microSD card to the host. Like the keyboard, the MSC *interface* is
 * always present in the descriptor the host first enumerates (the global USBMSC
 * object registers it before app_main), but it carries no media until the owner
 * hands the card over in the HID app. Until then the host sees a drive with
 * nothing in it - "please insert a disk" - rather than a broken one.
 *
 * The card has exactly one owner. While Mass Storage is active the device
 * unmounts /sd locally and hands the raw card to the host; FatFs and the host
 * writing the same card at once is corruption, so the two never overlap. That
 * ordering - unmount before expose, hide before remount - is the whole safety
 * of this surface and is enforced in catnip_usb_msc_enable().
 *
 * These calls are behind this header for the same reason the keyboard's are:
 * the rest of the firmware, and the Lua surface over it (service.usb.*), never
 * touches TinyUSB or the SD driver.
 */
#ifndef CATNIP_USB_MSC_H
#define CATNIP_USB_MSC_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configure the drive: the strings the host shows, the read/write callbacks,
 * and no media. The MSC interface was already registered before app_main by the
 * global USBMSC object's constructor, so this only fills in behaviour; call it
 * once in setup(), before USB.begin(). No media is present until the app calls
 * catnip_usb_msc_enable(true). */
void catnip_usb_msc_begin(void);

/* Hand the card to the host, or take it back. On true: flush and unmount /sd so
 * FatFs releases the card, take the raw card, then mark the media present - in
 * that order, so nothing but the host touches the card once it is exposed. On
 * false: mark the media absent, release the raw card, then remount /sd. A no-op
 * when already in the requested state, and a no-op with no card. */
void catnip_usb_msc_enable(bool on);

/* Whether the host currently owns the card (media present). False at boot. */
bool catnip_usb_msc_active(void);

/* Whether there is a card to hand over: one mounted locally now, or one already
 * handed to the host. The HID app greys Mass Storage when this is false. */
bool catnip_usb_msc_has_card(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_USB_MSC_H */
