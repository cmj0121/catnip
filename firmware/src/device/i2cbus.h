/*
 * i2cbus.h - the shared I2C bus.
 *
 * The PMIC, the I/O expander, the touch panel, the RTC and the IMU all live on
 * one bus, and several of them are needed before anything else works. Owning
 * the bus here keeps its bring-up out of whichever driver happens to run first.
 */
#ifndef CATNIP_I2CBUS_H
#define CATNIP_I2CBUS_H

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

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_I2CBUS_H */
