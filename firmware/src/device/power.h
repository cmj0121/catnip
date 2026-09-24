/*
 * power.h - hold the MeowKit's power rail on.
 *
 * The board latches its own supply through a GPIO. Until firmware asserts it,
 * the device is running on the momentary press of the power button; assert it
 * first, before anything slow, or the board switches off mid-boot.
 */
#ifndef CATNIP_POWER_H
#define CATNIP_POWER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Latch the power rail on. Safe to call more than once. */
void catnip_power_hold(void);

/* Release the latch and stop. Never returns: on battery the board loses its
 * supply within a moment, and on USB - where it does not - this halts instead,
 * so that "off" looks the same either way. Turning the device back on is a
 * power cycle, which is what the button does when nothing is running. */
void catnip_power_off(void);

/* Freeze the power-rail latch so it survives a software reset, without itself
 * restarting. The rail is then never interrupted across the reset the caller
 * triggers next - an esp_restart(), or a reboot into the ROM bootloader (Flash
 * Mode, #61) - so the board comes back rather than switching off, which on
 * battery is what a bare reset does here. catnip_power_hold() releases it once
 * the next boot has taken the pin over again. */
void catnip_power_hold_freeze(void);

/* Keep the rail latched across a software restart, then restart.
 *
 * ESP.restart() on its own switches this board off. PWR_HOLD is a level, not a
 * latch - catnip_power_off() cuts the rail by dropping it - and a reset returns
 * every pad to an input, so the rail goes before setup() can drive it again. On
 * USB the chip survives on VBUS; on battery it does not, and either way the
 * device does not come back the way "restart" promises.
 *
 * GPIO11 is an RTC pad on the ESP32-S3, so the hold survives the reset and the
 * pin goes on driving high through it. catnip_power_hold() releases the hold
 * once it has taken the pin over again. */
void catnip_power_restart(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_POWER_H */
