/*
 * imu_map.h - which screen edge the accelerometer says is pointing up.
 *
 * Kept apart from imu.cpp, and free of Arduino.h, for the same reason the
 * rotation is kept out of touch.cpp and the bounce filter out of input.cpp:
 * turning three acceleration values into an edge is arithmetic over a fixed
 * table, so it can be checked on the host instead of by turning a device over
 * and believing what appears. That leaves imu.cpp as I2C register reads and
 * nothing else worth testing.
 *
 * It also gives the mapping exactly one home. The correspondence between the
 * part's axes and the screen's edges is a GUESS - see the table in imu_map.c -
 * and the whole point of keeping it in one table is that correcting it is an
 * edit to that table and to nothing else. No caller reads an axis directly, so
 * no sign has to be chased through a driver or a drawing routine.
 */
#ifndef CATNIP_IMU_MAP_H
#define CATNIP_IMU_MAP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One half-axis, and what it means on the screen the user is looking at.
 *
 * `axis` and `sign` are the accelerometer's; `name` and the direction are the
 * screen's. Keeping all four in one row is what makes it impossible for the
 * words on the diagnostic page and the arrow drawn beside them to disagree:
 * they come from the same row or from no row at all.
 *
 * `dx` and `dy` are a unit direction in screen pixels - x right, y down, as
 * the display counts them - for an arrow drawn from the centre of the screen.
 * Both are zero for the two flat attitudes, where no screen edge is up and
 * pointing an arrow anywhere would be an invention. A caller draws no arrow
 * for those and says "flat" instead, which is the honest answer.
 */
typedef struct {
    uint8_t axis;
    int8_t sign;
    const char *name;
    int8_t dx;
    int8_t dy;
} catnip_imu_up;

/* The row for the current attitude, given the three acceleration axes in
 * milli-g, or NULL when no axis is dominant enough to name one.
 *
 * NULL is a real answer rather than a failure: the device is being moved, or
 * is balanced on a corner, and there genuinely is no edge pointing at the
 * ceiling. A caller must say so rather than keep drawing the last one, or the
 * page becomes a record of where the device used to be.
 *
 * The sign convention is the accelerometer's, not gravity's. At rest the part
 * measures the force holding it up, so the axis reading positive is the one
 * pointing at the ceiling. No negation is needed anywhere here and none should
 * be added.
 */
const catnip_imu_up *catnip_imu_up_edge(const int32_t mg[3]);

/* Below this, in milli-g, no axis is called dominant. Gravity is 1000 mg, so
 * half of it is a generous margin for a device merely held in the hand. It is
 * published because the diagnostic page has to be able to explain, on screen,
 * why it is not naming an edge. */
#define CATNIP_IMU_UP_MIN_MG 500

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_IMU_MAP_H */
