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

/* Sample what changes slowly: the battery. Call it from the main loop - it has
 * its own budget inside and returns without touching the bus most of the time.
 *
 * The budget is five seconds, which is far longer than the touch controller's
 * 12 ms or the IMU's 50 ms, and for the opposite reason. Those two are read
 * often because a hand moves fast. A lithium cell's terminal voltage moves over
 * minutes, so a faster poll would buy no accuracy at all and would spend the
 * shared bus that the touch controller and the IMU are waiting on. */
void catnip_pmu_poll(void);

/* The battery's charge as a percentage, 0-100, as of the last poll - or -1 when
 * there is nothing honest to report: the PMIC never answered, no battery is
 * connected (the device is running off USB), or the voltage register read back
 * a value no single-cell lithium battery can produce.
 *
 * That last case is deliberate and it is why this can say -1 even on a working
 * board. Unlike the rail registers above, which were confirmed by this firmware
 * setting them and the board staying up, the battery ADC's registers here come
 * from the AXP173 datasheet and have not been checked against a meter on this
 * unit. The vendor's documentation for this board has been wrong three times
 * already - the display's chip-select, all three button pins, the IMU's part
 * number - so a number decoded from an unconfirmed register is a guess. If the
 * map is wrong the reading lands outside what a lithium cell can be, and "I do
 * not know" is the true answer; clamping it into 0-100 would turn a wrong
 * register into a confident wrong percentage instead.
 *
 * The percentage itself is an estimate from voltage, not a fuel gauge - see
 * battery_gauge.h, where the curve and the refusal both live and where a host
 * test drives them. It reads low under load and recovers when the load goes. */
int catnip_pmu_battery_percent(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_PMU_H */
