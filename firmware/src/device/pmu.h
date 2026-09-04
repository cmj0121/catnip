/*
 * pmu.h - the AXP173 power management IC.
 *
 * Every supply on this board comes from here. The MCU runs from DCDC1, so by
 * the time any of this executes DCDC1 is necessarily up - but the rails that
 * feed the I/O expander, the SD card and the sensors are not implied, and a
 * peripheral on a rail that is off is simply absent from its bus.
 */
#ifndef CATNIP_PMU_H
#define CATNIP_PMU_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the rails the firmware needs. Returns true if the PMIC answered.
 * Logs what it found and what it changed, because a rail that was already on
 * and one that was just switched on look identical afterwards. */
bool catnip_pmu_begin(void);

/* True once for each short press of the power button, which reaches the MCU
 * only through the PMIC - see board.h. Clears the latch as it reads it, so a
 * press is reported to exactly one caller. */
bool catnip_pmu_power_key_pressed(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_PMU_H */
