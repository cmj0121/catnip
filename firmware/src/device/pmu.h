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
/* The AXP173's power-key interrupt: one register, two bits, both cleared by
 * writing a 1 back to them.
 *
 * These are up here rather than private to pmu.cpp because the power button
 * does not reach the MCU at all - board.h records GPIO10 reading high whether
 * or not it is down - so this register is the only place anything on this
 * board can learn that the button was pressed. That makes it a fact about the
 * part that more than one piece of code needs: the probe in probe_input.cpp
 * watches and annotates the very same register while it is sweeping every
 * other input, and had it kept its own copy of these numbers the two would
 * have been free to drift apart with nothing to catch it. */
#define CATNIP_PMU_REG_IRQ_STATUS_3 0x46
#define CATNIP_PMU_IRQ_PEK_SHORT    (1u << 1)
#define CATNIP_PMU_IRQ_PEK_LONG     (1u << 0)

bool catnip_pmu_begin(void);

/* True once for each short press of the power button, which reaches the MCU
 * only through the PMIC - see board.h. Clears the latch as it reads it, so a
 * press is reported to exactly one caller. */
bool catnip_pmu_power_key_pressed(void);

/* True once when the power button has been held down. The PMIC decides how
 * long "held" is; acting on it is the firmware's business - see
 * catnip_power_off(). */
bool catnip_pmu_power_key_held(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_PMU_H */
