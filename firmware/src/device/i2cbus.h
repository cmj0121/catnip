/*
 * i2cbus.h - the shared I2C bus.
 *
 * The PMIC, the I/O expander, the touch panel, the RTC and the IMU all live on
 * one bus, and several of them are needed before anything else works. Owning
 * the bus here keeps its bring-up out of whichever driver happens to run first.
 */
#ifndef CATNIP_I2CBUS_H
#define CATNIP_I2CBUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the bus and let it settle. Devices on switched rails answer nothing
 * until their supply is up, so this waiting is not politeness - without it an
 * expander that is about to work looks absent. */
void catnip_i2c_begin(void);

/* Log every address that answers. When something is missing this separates a
 * dead bus from a device on a rail that is still off. */
void catnip_i2c_scan(void);

/* The same sweep, answered rather than printed: fills `out` with the addresses
 * that acknowledged, up to `max`, and returns how many. The device info page
 * asks it again rather than reading what the boot scan found, because a bus is
 * a live thing - a part that has stopped answering since boot is exactly what
 * somebody reading that page is looking for. */
int catnip_i2c_present(uint8_t *out, int max);

/* One-byte register access, the shape every chip on this bus speaks. Both
 * return false when the device did not acknowledge. */
bool catnip_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value);
bool catnip_i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *out);

/* Read `len` consecutive registers starting at `reg` in one transaction, for
 * the parts on this bus that auto-increment their register pointer. Returns
 * false, and writes nothing, when the device did not acknowledge or returned
 * short.
 *
 * This is here rather than in a driver because the saving is the bus's, not
 * any one chip's. Every catnip_i2c_read_reg() is a write-reg, a repeated
 * start and a one-byte read; asking for five consecutive registers one at a
 * time pays that overhead five times on a 400 kHz bus that the PMIC, the I/O
 * expander, the touch controller, the RTC and the IMU all share. Whoever is
 * reading a run of registers is taking time from all of them. */
bool catnip_i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *out, size_t len);

/* Write `len` bytes to `reg` in one transaction. Returns false when the device
 * did not acknowledge.
 *
 * `len` must fit the Wire library's transmit buffer alongside the register
 * byte - 128 bytes on this core, so 127 payload bytes at the very most. There
 * is no check here because the one caller picks its own chunk size and states
 * it; a driver that hands this a run longer than the bus can carry has a bug
 * that a silently short write would hide.
 *
 * This exists for the IMU. A BMI270 will not produce a single reading until an
 * 8192-byte configuration image has been pushed into it, and it takes that
 * image through one register that is written over and over. Sending it with
 * catnip_i2c_write_reg() would be 8192 separate start-address-data-stop
 * transactions on a 400 kHz bus the PMIC, the expander, the RTC and the touch
 * controller also share - most of a second of the bus held for framing rather
 * than payload, once per boot. */
bool catnip_i2c_write_regs(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_I2CBUS_H */
