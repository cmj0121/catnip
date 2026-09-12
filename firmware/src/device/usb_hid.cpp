/* usb_hid.cpp - see usb_hid.h. */
#include "usb_hid.h"

/* The composite keyboard exists only on a TinyUSB build (USB_MODE=0), which is
 * what the shipping meowkit env now is. On any other build - the poc/probe
 * sketches, which keep the USB-Serial/JTAG mode - the four calls are stubs, so
 * this file compiles everywhere and the symbols the HAL wires are always there;
 * catnip_usb_hid_enabled() simply answers false, and nothing types. */
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0

#include <string.h>

#include <Arduino.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

namespace {

/* Constructed at static-init, before app_main and therefore before the core's
 * USB.begin(): the constructor registers the HID interface, so the device the
 * host first enumerates already carries a keyboard beside the CDC console. It is
 * the library's boot keyboard - the report shape every host already has a driver
 * for, which is the point of not rolling our own here. */
USBHIDKeyboard g_kbd;

/* The arm gate. False at boot; while false catnip_usb_hid_key() returns before
 * sending anything. The keyboard is present and idle. */
bool g_armed = false;
bool g_begun = false;

} /* namespace */

void catnip_usb_hid_begin(void)
{
    if (g_begun) return;
    g_begun = true;
    /* Starts the HID class and, through it, USB.begin() - idempotent with the
     * core's own begin when CDC is on at boot. */
    g_kbd.begin();
    USB.begin();
    Serial.println("[catnip] usb-hid: keyboard present, typing disabled");
}

void catnip_usb_hid_enable(bool on)
{
    if (on == g_armed) return;
    g_armed = on;
    Serial.printf("[catnip] usb-hid: typing %s\n", on ? "enabled" : "disabled");
}

bool catnip_usb_hid_enabled(void)
{
    return g_armed;
}

void catnip_usb_hid_key(unsigned char mods, unsigned char usage)
{
    if (!g_armed) return;

    /* One keystroke: a report with the modifiers held and the usage in the
     * first key slot, then an all-zero report to release. The small waits give
     * the host time to see each edge - a press and its release sent back to
     * back are dropped by more than one operating system. */
    KeyReport r;
    memset(&r, 0, sizeof(r));
    r.modifiers = mods;
    r.keys[0] = usage;
    g_kbd.sendReport(&r);
    delay(5);
    memset(&r, 0, sizeof(r));
    g_kbd.sendReport(&r);
    delay(5);
}

#else /* not a TinyUSB build: no HID keyboard here. */

void catnip_usb_hid_begin(void)
{
}
void catnip_usb_hid_enable(bool on)
{
    (void)on;
}
bool catnip_usb_hid_enabled(void)
{
    return false;
}
void catnip_usb_hid_key(unsigned char mods, unsigned char usage)
{
    (void)mods;
    (void)usage;
}

#endif
