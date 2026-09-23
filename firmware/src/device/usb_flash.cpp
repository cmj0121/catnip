/* usb_flash.cpp - see usb_flash.h. */
#include "usb_flash.h"

/* Only the composite build (USB_MODE=0) has a bootloader to fall back to and a
 * TinyUSB stack to leave behind; on any other build the reboot is a stub, the
 * same shape usb_hid.cpp and usb_msc.cpp compile under. */
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0

#include <Arduino.h>

/* usb_persist_restart() and RESTART_BOOTLOADER live here. It is the same call
 * the Arduino core's own USBCDC makes when the host does the DTR/RTS reset
 * sequence or the 1200 bps touch (cores/esp32/USBCDC.cpp); doing it explicitly
 * from an app does not depend on esptool getting those line toggles through the
 * composite, which is the whole reason this exists as a deliberate action. */
#include "esp32-hal-tinyusb.h"

#include "display.h"
#include "led.h"
#include "power.h"

void catnip_usb_flash_mode(void)
{
    Serial.println("[catnip] usb-flash: going dark, rebooting into ROM download mode");
    Serial.flush();

    /* Go dark cleanly first, so the screen that stays dark is the honest sign
     * download mode took (docs/INSTALL.md) and not a frozen last frame with the
     * LED still breathing at it: the status LED off, the panel cleared, and its
     * backlight down. */
    catnip_led_level(0);
    catnip_display_fill(0x0000);
    catnip_display_backlight(0);

    /* The catch this board sets: a bare reset drops PWR_HOLD and, on battery,
     * switches the device off instead of rebooting - so it would never reach the
     * bootloader. Freeze the rail across the reset first, exactly as a normal
     * software restart does (power.cpp). */
    catnip_power_hold_freeze();

    /* Persists the USB device across the restart and comes back up in the ROM
     * serial bootloader, ready for esptool. Does not return. */
    usb_persist_restart(RESTART_BOOTLOADER);
}

#else /* not a TinyUSB build: no bootloader dance to do. */

void catnip_usb_flash_mode(void)
{
}

#endif
