/* imu_map.c - see imu_map.h. */
#include "imu_map.h"

#include <stdlib.h> /* labs() */

/* THE FOUR SCREEN EDGES ARE CONFIRMED on this unit, the way the handedness in
 * touch_map.c was confirmed by a finger: the device was held with each screen
 * edge in turn pointing at the ceiling, and the diagnostic page's arrow pointed
 * at the ceiling every time. Four orientations, four correct answers. The first
 * four rows below are a measurement.
 *
 * The two Z rows are NOT. See the paragraph that says so, below.
 *
 * The derivation that produced the four rows is kept, because it explains why
 * they are shaped as they are and stops someone rearranging them on intuition.
 * The panel is a 240x320 part driven at CATNIP_LCD_ROTATION 3 so that the user
 * sees a 320x240 landscape screen, and the IMU is a separate package soldered
 * somewhere on the board. The datasheet says where the chip's axes lie relative
 * to its own package and nothing at all about where the package lies relative
 * to the screen, so the correspondence could not be derived - only predicted
 * and then checked.
 *
 * What was predicted, and has now been borne out: the part is placed square
 * with the board outline, its +X running along the panel's X axis (across the
 * panel's short edge, 0..239) and its +Y along the panel's Y axis (down the
 * long edge, 0..319), with +Z out through the front of the glass. Rotation 3
 * maps a panel point to the screen as screen_x = PANEL_H - 1 - panel_y and
 * screen_y = panel_x - the same arithmetic catnip_touch_panel_to_screen() does
 * - so increasing panel X walks DOWN the screen and increasing panel Y walks
 * LEFT across it. Hence +X toward the ceiling means the bottom edge is up, and
 * +Y toward the ceiling means the left edge is up. Both of those were read off
 * the screen rather than argued for.
 *
 * So the four horizontal rows are now measured rather than assumed, and the
 * corrections this comment used to hold in reserve - swapping names between two
 * rows of the same axis, or moving a name between axes - would now be the bug
 * rather than the fix.
 *
 * THE TWO FLAT ROWS ARE STILL UNVERIFIED. The four edges were exercised; the
 * two flat attitudes were not, and they are the ones that are hard to catch,
 * because they draw no arrow. A wrong Z row shows up as nothing happening
 * rather than as something visibly wrong, which is the error that survives
 * being looked at. What would settle them: lay the device flat on a desk face
 * up, and read the headline - it should say "flat, screen up" - then turn it
 * face down and read "flat, screen down". If those two are swapped the fix is
 * to exchange the names between the last two rows, and nothing else changes.
 *
 * TO REPEAT THE EDGE CHECK ON ANOTHER UNIT, which is a one-minute job: put
 * /sd/catnip/diag on the card or type "diag" over serial, then turn the device
 * so each screen edge in turn points at the ceiling and confirm the arrow
 * follows it - four turns, and the page names the edge in words beside the
 * arrow so nothing has to be inferred from a log.
 *
 * The two flat rows carry a zero direction rather than a missing one. Screen
 * up and screen down are perfectly good attitudes and the page names them; it
 * is only the arrow that has nowhere to point, because no screen edge is any
 * closer to the ceiling than any other. */
static const catnip_imu_up kUpEdge[] = {
    /* Measured: each of these four was held toward the ceiling and the page's
     * arrow followed it. */
    {0, +1, "bottom edge", 0, +1},
    {0, -1, "top edge", 0, -1},
    {1, +1, "left edge", -1, 0},
    {1, -1, "right edge", +1, 0},
    /* Not yet measured. These two draw no arrow, so getting them the wrong way
     * round would look like nothing at all rather than like a mistake. Lay the
     * device flat each way up and read the headline to settle them. */
    {2, +1, "flat, screen up", 0, 0},
    {2, -1, "flat, screen down", 0, 0},
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
