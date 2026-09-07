/*
 * imu.h - the accelerometer: which way gravity points, and which screen edge
 * that makes the top one.
 *
 * The part was expected to be a QMI8658A on the shared I2C bus at 0x68 - that
 * is what hal_meowkit.cpp named in a comment - and IT IS NOT. It is a Bosch
 * BMI270. The vendor's documentation was wrong about it, as it was already
 * wrong about the display's chip-select, the display's reset and all three of
 * the published button pins.
 *
 * That was established by reading, not by guessing. A read-only dump taken
 * twice a hundred milliseconds apart gave register 0x00 = 0x24, which is a
 * BMI270's CHIP_ID and not the 0x05 a QMI8658A reports; 0x75 = 0x00, which
 * rules out the MPU-6000/6050/6886 family that keeps its identity there; and
 * STATUS = 0x10 beside INTERNAL_STATUS = 0x00, which is cmd_rdy set with
 * not_init - a BMI270 that has powered up and never been configured. Nothing
 * moved between the two passes, because a BMI270 in that state produces no
 * data at all. board.h records the same numbers beside the address.
 *
 * A BMI270 is not a chip you configure with a handful of register writes. It
 * boots with no firmware and stays silent until an 8192-byte configuration
 * image supplied by Bosch has been pushed into it, and it is only after that
 * image lands that INTERNAL_STATUS reads init_ok and the part becomes an
 * accelerometer at all. imu.cpp carries that upload and explains its one
 * genuinely surprising detail; lib/bmi270/ carries the image and its licence.
 *
 * Reaching init_ok is also what finally confirms the part. Everything above
 * identifies it without ever having written to it, which is a strong argument
 * and not a proof: a chip that accepts Bosch's image and reports init_ok has
 * demonstrated it is a BMI270 rather than merely resembled one.
 *
 * Only the accelerometer is brought up. The gyroscope is left off because the
 * question this driver exists to answer is which way is down, and a gyroscope
 * cannot answer it; turning it on would cost current and bus time in support
 * of no caller. The feature engine the image also carries - step counting,
 * wrist gestures, tap detection - is left alone for exactly the same reason.
 *
 * The axis-to-edge mapping is NOT here. It lives in imu_map.h, where a host
 * test drives it and where any correction is made in one place - the same
 * arrangement as the touch rotation, and for the same reason: how a part is
 * mounted cannot be derived, only measured, and a measurement should have
 * exactly one home. It has now been measured, the way the touch rotation was:
 * the device was turned so each screen edge in turn pointed at the ceiling and
 * the diagnostic page's arrow followed it all four times. The two flat
 * attitudes were not exercised and are still marked unverified in imu_map.c.
 *
 * Exposing this to Lua as sensor.imu is issue #35's. That was deliberately held
 * until the mapping had been measured, so that no app would inherit a guess;
 * for the four screen edges it now has been.
 */
#ifndef CATNIP_IMU_H
#define CATNIP_IMU_H

#include <stdbool.h>
#include <stdint.h>

#include "imu_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Confirm the part, upload its configuration image, wake the accelerometer and
 * read its range back. The I2C bus must already be up (catnip_i2c_begin()),
 * because that is shared and not this driver's to own.
 *
 * SAFE TO CALL MORE THAN ONCE, and cheap when it is. The first call that finds
 * an unconfigured part takes roughly a quarter of a second, because the image
 * is 8 KB over a 400 kHz bus and there is no way to pay less of it. Every call
 * after that reads INTERNAL_STATUS, finds init_ok, and skips the upload - a
 * single transaction instead of a hundred and twenty-eight - while still
 * asserting the accelerometer configuration, so the postcondition is the same
 * either way.
 *
 * That is decided by asking the part, not by remembering having asked. A
 * BMI270 does not retain the image across a power cycle and the sensors sit on
 * a rail the PMIC can switch, so a part that lost power reads not_init again
 * and is re-uploaded. "Idempotent" here means converging on a working
 * accelerometer, not doing the work only once per boot.
 *
 * This exists because two callers want the IMU - the HAL at boot and the
 * diagnostic page when it takes the screen - and it belongs here rather than
 * in either of them: a caller should be able to ask for the IMU without
 * knowing who asked first, and the driver is the only thing that knows its own
 * state. The log distinguishes the two outcomes, because two identical success
 * lines for two different events is how a duplicated 8 KB upload reached a
 * device unnoticed.
 *
 * Returns true only when 0x68 reported a BMI270's CHIP_ID, accepted the image,
 * reached init_ok and accepted its accelerometer configuration. The identity
 * check before the first byte is written is not ceremony, and it is not
 * relaxed now that the answer is known: it is the check that caught this part
 * being mis-documented in the first place, and a second unit could yet be
 * built with something else at 0x68. An accelerometer's registers decode into
 * plausible numbers whatever chip produced them, so some other part here would
 * yield a confident, wrong, stable orientation - which reads as a mounting
 * that needs correcting rather than as the wrong chip. That confusion is why
 * the mapping in imu_map.c could only be trusted once the identity was settled
 * first, and it is why a unit that answers something other than 0x24 gets
 * refused rather than interpreted: its arrow would invite an edit to a table
 * that is now known to be right.
 *
 * When this returns false the driver reports nothing for the rest of the run,
 * and catnip_imu_who_am_i() and catnip_imu_internal_status() are what say why.
 */
bool catnip_imu_begin(void);

/* What 0x68 said when asked who it is: true when a register read was answered
 * at all, with the raw byte written to `out`.
 *
 * This is separate from catnip_imu_begin()'s result because a diagnostic has
 * to tell several cases apart and they are not the same failure: nothing at
 * 0x68 at all, something at 0x68 that is not a BMI270, and a BMI270 working.
 * The raw byte is the one number that decides whether anything else this
 * driver reports means anything, so it is published rather than only logged.
 *
 * Answering is not the same as being recognised, and this deliberately does
 * not say which. It reports what was read; catnip_imu_begin() is what judges
 * it.
 */
bool catnip_imu_who_am_i(uint8_t *out);

/* What INTERNAL_STATUS read after the configuration image was uploaded: true
 * when the register was answered at all, with the raw byte written to `out`,
 * and false when the upload was never attempted because the identity check
 * had already refused the part.
 *
 * Published for the same reason the identity is. A CHIP_ID of 0x24 with an
 * upload that never reached init_ok is a different fault from a chip that is
 * not a BMI270 and from one that is working, and all three look identical from
 * an absent arrow. This is the register that separates them, and it is also
 * the register that confirms the part, so the diagnostic page prints it rather
 * than leaving it in a boot log nobody is watching by then.
 */
bool catnip_imu_internal_status(uint8_t *out);

/* What a BMI270 reports at register 0x00, and what INTERNAL_STATUS's message
 * field reads once its configuration image has been accepted. Published rather
 * than kept in imu.cpp because the diagnostic page prints the expected values
 * next to the ones that were actually read - that comparison is the point of
 * showing the numbers at all - and two copies of them are two things that can
 * drift. */
#define CATNIP_IMU_CHIP_ID_BMI270 0x24
#define CATNIP_IMU_INIT_OK        0x01

/* INTERNAL_STATUS's low nibble is the message; the bits above it are separate
 * error flags. A caller comparing the whole byte against init_ok would call a
 * successfully initialised part broken the moment one of those flags was set,
 * so the mask is published beside the value it is meant to be used with. */
#define CATNIP_IMU_INIT_MSG_MASK 0x0F

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
