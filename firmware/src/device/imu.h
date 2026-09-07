/*
 * imu.h - the accelerometer: which way gravity points, and which screen edge
 * that makes the top one.
 *
 * The part was expected to be a QMI8658A on the shared I2C bus at 0x68 -
 * that is what hal_meowkit.cpp names in a comment - and IT IS NOT. Asked for
 * its identity on this unit, 0x68 reports 0x24 where a QMI8658A reports 0x05.
 * The vendor's documentation was wrong about it, as it was already wrong about
 * the display's chip-select, the display's reset and all three of the
 * published button pins.
 *
 * So this driver currently identifies nothing and reports nothing, which is
 * the correct behaviour rather than a gap waiting to be filled. Had it trusted
 * the comment and read the register map anyway, the acceleration registers of
 * whatever is really there would have decoded into entirely plausible numbers,
 * and a wrong arrow would have been read as a mounting that needed correcting
 * - the one failure catnip_touch_begin()'s identity check exists to prevent,
 * and the reason this one is shaped the same way.
 *
 * What happens next is identification, not a driver. catnip_imu_begin() dumps
 * the registers that tell the candidate families apart, over serial, writing
 * nothing to the part; imu.cpp says which registers and why. There is a
 * standing hypothesis about what it is, and it stays a hypothesis until those
 * registers settle it.
 *
 * Only the accelerometer is brought up. The gyroscope is left off because the
 * question this driver exists to answer is which way is down, and a gyroscope
 * cannot answer it; turning it on would cost current and bus time in support
 * of no caller.
 *
 * The axis-to-edge mapping is NOT here. It lives in imu_map.h, where a host
 * test drives it and where the one correction it may still need can be made in
 * one place - the same arrangement as the touch rotation, and for the same
 * reason: it is a guess about how a part is mounted, and a guess should have
 * exactly one home.
 *
 * Exposing this to Lua as sensor.imu is issue #35's, and deliberately comes
 * after the mapping has been measured. Handing apps an orientation that is
 * still a guess would spread the guess into every app that reads it.
 */
#ifndef CATNIP_IMU_H
#define CATNIP_IMU_H

#include <stdbool.h>
#include <stdint.h>

#include "imu_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Confirm the part, wake the accelerometer and read its range back. The I2C
 * bus must already be up (catnip_i2c_begin()), because that is shared and not
 * this driver's to own.
 *
 * Returns true only when a QMI8658A answered and accepted its configuration,
 * which on this unit it does not. That check is not ceremony, and it is not the
 * same check the touch driver makes for the same reason twice over. An accelerometer's registers decode
 * into plausible numbers whatever chip produced them: some other part at 0x68
 * would yield a confident, wrong, stable orientation, which reads as a mounting
 * that needs correcting rather than as the wrong chip - and the mounting is
 * precisely what is still unknown. Refusing to guess is what keeps those two
 * apart.
 *
 * When this returns false the driver reports nothing for the rest of the run,
 * and catnip_imu_who_am_i() is what says why.
 */
bool catnip_imu_begin(void);

/* What 0x68 said when asked who it is: true when a register read was answered
 * at all, with the raw byte written to `out`.
 *
 * This is separate from catnip_imu_begin()'s result because a diagnostic has
 * to tell three cases apart, and only two of them are failures of the same
 * kind: nothing at 0x68 at all, something at 0x68 that is not a QMI8658A, and
 * a QMI8658A working. The raw byte is the one number that decides whether
 * anything else this driver reports means anything, so it is published rather
 * than only logged.
 *
 * Answering is not the same as being recognised, and this deliberately does
 * not say which. It reports what was read; catnip_imu_begin() is what judges
 * it.
 */
bool catnip_imu_who_am_i(uint8_t *out);

/* What a QMI8658A reports at register 0x00. Published rather than kept in
 * imu.cpp because the diagnostic page prints the expected value next to the
 * one that was actually read - that comparison is the point of showing the
 * number at all - and two copies of it are two things that can drift. */
#define CATNIP_IMU_WHO_AM_I_QMI8658A 0x05

/* Sample the accelerometer: six consecutive registers in one transaction.
 * Call it from the main loop.
 *
 * A read the part does not acknowledge leaves the last reading standing. A
 * device does not change attitude because the bus was busy, and inventing a
 * new orientation from a failed transaction would put an arrow somewhere the
 * device is not pointing.
 */
void catnip_imu_poll(void);

/* The three acceleration axes in milli-g, as of the last successful poll.
 * Returns false, and writes nothing, when no reading has ever been taken -
 * before the first poll, or when the part is absent.
 *
 * Milli-g rather than a float: a fixed-point integer is exact here, and the
 * one place these numbers are printed cannot rely on printf's float support
 * being compiled into this build's newlib. The probe hit that already.
 *
 * The axes are the part's own, in its own frame. Which screen edge each one
 * points at is catnip_imu_orientation()'s business, and a caller that wants an
 * orientation should ask for one rather than reading a sign out of here.
 */
bool catnip_imu_acceleration(int32_t mg[3]);

/* The screen edge pointing at the ceiling, as of the last successful poll, or
 * NULL when there is none to name - the part is absent, nothing has been read
 * yet, or no axis is dominant because the device is moving or on a corner. A
 * caller must say so rather than keep drawing the last answer. */
const catnip_imu_up *catnip_imu_orientation(void);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_IMU_H */
