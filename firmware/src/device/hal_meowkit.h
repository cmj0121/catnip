/*
 * hal_meowkit.h - the MeowKit's drivers, wired into the table Lua calls through.
 *
 * catnip_hal.h is the whole of what a Lua app can reach; this is where the
 * pointers in it are filled with things that touch the board. There are exactly
 * two entry points, and main.cpp calls one of each, so installing the hardware
 * is two lines rather than a list of driver calls that has to be kept in step
 * with this file.
 */
#ifndef CATNIP_HAL_MEOWKIT_H
#define CATNIP_HAL_MEOWKIT_H

extern "C" {
#include "../catnip_hal.h"
}

/* Bring up the drivers this HAL owns and return the filled table, which is
 * static and outlives the runtime as catnip_api_open() requires.
 *
 * Call it after catnip_i2c_begin(), catnip_pmu_begin() and the display, since
 * this brings up only what nothing else has: the switches and the IMU. It costs
 * about a quarter of a second, nearly all of it the BMI270's 8 KB configuration
 * image going out over a 400 kHz bus - see imu.h, where that cost is explained
 * and where it is established that there is no way to pay less of it.
 *
 * Never returns NULL. A driver that fails to come up leaves its own hooks
 * reporting nothing rather than taking the rest of the table down with it. */
const catnip_hal *catnip_meowkit_hal_begin(void);

/* Sample the hardware the hooks read from. Call it once per pass of the main
 * loop.
 *
 * This exists so that no Lua call polls hardware. A script in a tight loop
 * calling device.button() would otherwise be free to read pins - or, worse,
 * drive the shared I2C bus through sensor.imu() - as fast as the interpreter
 * runs, and the cost would land on the touch controller and the display rather
 * than on the script. Polling here instead puts a ceiling on it that no app can
 * raise, and has the second benefit that every hook called within one pass sees
 * one consistent snapshot of the device rather than a different instant each.
 *
 * The drivers underneath keep their own budgets, so calling this every pass is
 * cheap: the IMU reads at most every 50 ms and the battery at most every five
 * seconds, and the switches are plain pins that touch no bus at all. */
void catnip_meowkit_hal_poll(void);

#endif /* CATNIP_HAL_MEOWKIT_H */
