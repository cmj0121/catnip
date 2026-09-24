/* usb_flash.cpp - see usb_flash.h. */
#include "usb_flash.h"

/* Only the composite build (USB_MODE=0) has a bootloader to fall back to and a
 * TinyUSB stack to leave behind; on any other build the reboot is a stub, the
 * same shape usb_hid.cpp and usb_msc.cpp compile under. */
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0

#include <Arduino.h>

/* Not usb_persist_restart(RESTART_BOOTLOADER), the Arduino core's own route
 * (the one its USBCDC takes on the DTR/RTS reset sequence or the 1200 bps
 * touch). Tried on hardware (#95): the device went dark and left the bus for
 * good - no USB-Serial/JTAG unit ever enumerated, not even during the core's
 * switch to it, while every cold boot shows that unit within half a second. The
 * core ends in esp_restart(), which - the working guess, not yet confirmed -
 * does not reach the USB blocks, so the ROM wakes into whatever state TinyUSB
 * left behind.
 *
 * So do the reset ourselves, one level deeper: a core reset (SW_SYS_RST)
 * resets the whole digital side - USB-Serial/JTAG, USB-OTG, IO_MUX - the way a
 * cold boot does, while the RTC domain survives with the three things this
 * needs: the force-download flag, the PHY selection, and the PWR_HOLD pad
 * hold. */
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

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

    /* The PHY choice lives in the RTC domain, so the reset leaves it alone:
     * hand the pads back to the USB-Serial/JTAG unit, which is what the ROM
     * download talks over (the face the BOOT dance brings up). And let the
     * reset reach the USB and IO_MUX blocks rather than sparing them. */
    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,
                        RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL |
                            RTC_CNTL_USB_PAD_ENABLE | RTC_CNTL_USB_RESET_DISABLE |
                            RTC_CNTL_IO_MUX_RESET_DISABLE);

    /* Boot into the ROM serial bootloader, ready for esptool. Does not return. */
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    SET_PERI_REG_MASK(RTC_CNTL_OPTIONS0_REG, RTC_CNTL_SW_SYS_RST);
    for (;;) {
    }
}

#else /* not a TinyUSB build: no bootloader dance to do. */

void catnip_usb_flash_mode(void)
{
}

#endif
