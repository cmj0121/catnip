/* imu_map.c - see imu_map.h. */
#include "imu_map.h"

#include <stdlib.h> /* labs() */

/* THE TABLE BELOW IS A GUESS, in the same sense and for the same reason as the
 * handedness in touch_map.c was until a finger settled it: nothing measured
 * supports it yet.
 *
 * What it is a guess about. The panel is a 240x320 part driven at
 * CATNIP_LCD_ROTATION 3 so that the user sees a 320x240 landscape screen, and
 * the IMU is a separate package soldered somewhere on the board. The datasheet
 * says where the chip's axes lie relative to its own package and nothing at
 * all about where the package lies relative to the screen, so the
 * correspondence cannot be derived - only measured.
 *
 * The guess assumes the ordinary case: the part is placed square with the
 * board outline, its +X running along the panel's X axis (across the panel's
 * short edge, 0..239) and its +Y along the panel's Y axis (down the long edge,
 * 0..319), with +Z out through the front of the glass. Rotation 3 maps a panel
 * point to the screen as screen_x = PANEL_H - 1 - panel_y and screen_y =
 * panel_x - the same arithmetic catnip_touch_panel_to_screen() does - so
 * increasing panel X walks DOWN the screen and increasing panel Y walks LEFT
 * across it. Hence +X toward the ceiling means the bottom edge is up, and +Y
 * toward the ceiling means the left edge is up.
 *
 * HOW TO CORRECT IT. Put the input diagnostic page on the screen, turn the
 * device so that each screen edge in turn points at the ceiling, and read what
 * the page says and where its arrow points. If an edge is named wrongly the
 * fix is these six rows and nothing else: swap the names between two rows of
 * the same axis to correct a sign, or move a name between axes to correct a
 * transposition. The arrow direction travels with the name in the same row, so
 * it cannot be corrected out of step with it.
 *
 * The two flat rows carry a zero direction rather than a missing one. Screen
 * up and screen down are perfectly good attitudes and the page names them; it
 * is only the arrow that has nowhere to point, because no screen edge is any
 * closer to the ceiling than any other. */
static const catnip_imu_up kUpEdge[] = {
    {0, +1, "bottom edge", 0, +1},    {0, -1, "top edge", 0, -1},
    {1, +1, "left edge", -1, 0},      {1, -1, "right edge", +1, 0},
    {2, +1, "flat, screen up", 0, 0}, {2, -1, "flat, screen down", 0, 0},
};

const catnip_imu_up *catnip_imu_up_edge(const int32_t mg[3])
{
    uint8_t axis = 0;
    int8_t sign;
    size_t i;

    for (i = 1; i < 3; i++) {
        if (labs(mg[i]) > labs(mg[axis])) axis = (uint8_t)i;
    }

    /* Not merely the largest axis, but large enough to mean something. A
     * device in mid-turn has all three axes small and one of them nominally
     * the biggest, and naming an edge from that would make the page report an
     * attitude the device is not in. */
    if (labs(mg[axis]) < CATNIP_IMU_UP_MIN_MG) return NULL;

    sign = mg[axis] >= 0 ? (int8_t)1 : (int8_t)-1;
    for (i = 0; i < sizeof(kUpEdge) / sizeof(kUpEdge[0]); i++) {
        if (kUpEdge[i].axis == axis && kUpEdge[i].sign == sign) return &kUpEdge[i];
    }

    /* Unreachable while the table holds all six half-axes, which the host test
     * checks. It is here because the loop above has to end somehow and a
     * silent fall through the bottom of a function would be worse. */
    return NULL;
}
