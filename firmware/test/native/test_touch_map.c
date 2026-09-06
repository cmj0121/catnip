/* Native test for issue #42: the rotation behind the touch driver.
 *
 * The touch controller reports where the finger is on a 240x320 panel; the
 * user is looking at a 320x240 screen, because the panel is mounted sideways
 * and driven at rotation 3. Getting that turn wrong is not a subtle failure -
 * taps land somewhere else entirely - but it is a silent one on a host, so it
 * is pinned down here by mapping the panel's corners and its centre to the
 * screen positions they have to come out at.
 *
 * Read the corner expectations as the specification of a handedness that is
 * not yet confirmed against the device (see touch_map.c). They are the
 * assumption written down where a corner capture can contradict it: when the
 * capture arrives, this test is what says whether the driver already agrees
 * with it, and if it does not, the four corners below and the two lines in
 * touch_map.c change together and nothing else has to. */
#include <stdio.h>

#include "device/board.h"
#include "device/touch_map.h"

static int failures;
#define CHECK(cond, name)                                                                \
    do {                                                                                 \
        if (cond) {                                                                      \
            printf("  ok   - %s\n", name);                                               \
        } else {                                                                         \
            printf("  FAIL - %s\n", name);                                               \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

/* Map one point and check it lands where it should. */
static void expect(uint16_t px, uint16_t py, uint16_t want_x, uint16_t want_y,
                   const char *name)
{
    uint16_t sx = 0xFFFF, sy = 0xFFFF;

    if (!catnip_touch_panel_to_screen(px, py, &sx, &sy)) {
        printf("  FAIL - %s (the panel point was rejected)\n", name);
        failures++;
        return;
    }
    if (sx == want_x && sy == want_y) {
        printf("  ok   - %s\n", name);
    } else {
        printf("  FAIL - %s: panel (%u, %u) gave screen (%u, %u), wanted (%u, %u)\n",
               name, px, py, sx, sy, want_x, want_y);
        failures++;
    }
}

int main(void)
{
    const uint16_t panel_max_x = CATNIP_LCD_PANEL_W - 1;
    const uint16_t panel_max_y = CATNIP_LCD_PANEL_H - 1;
    const uint16_t screen_max_x = CATNIP_SCREEN_W - 1;
    const uint16_t screen_max_y = CATNIP_SCREEN_H - 1;
    uint16_t sx, sy;
    uint16_t px, py;

    printf("== touch panel-to-screen rotation ==\n");

    /* The four corners of the glass, as the assumed handedness places them.
     * Walking the panel's Y axis walks left across the screen, and the panel's
     * X axis walks down it. */
    expect(0, 0, screen_max_x, 0, "the panel origin is the screen's top right");
    expect(0, panel_max_y, 0, 0, "the far end of the panel's Y axis is the top left");
    expect(panel_max_x, 0, screen_max_x, screen_max_y,
           "the far end of the panel's X axis is the bottom right");
    expect(panel_max_x, panel_max_y, 0, screen_max_y,
           "the opposite corner of the panel is the bottom left");

    /* The centre is the one point both candidate mappings agree on, which is
     * exactly why it cannot be left as the only case. */
    expect(CATNIP_LCD_PANEL_W / 2, CATNIP_LCD_PANEL_H / 2, CATNIP_SCREEN_W / 2 - 1,
           CATNIP_SCREEN_H / 2, "the middle of the panel is the middle of the screen");

    /* The three readings actually taken from the device. Nothing here says
     * where they should land - that is the unconfirmed part - only that a real
     * reading maps to a real screen position rather than off the edge. */
    {
        static const uint16_t measured[3][2] = {{212, 273}, {63, 158}, {118, 205}};
        int i, inside = 0;

        for (i = 0; i < 3; i++) {
            if (catnip_touch_panel_to_screen(measured[i][0], measured[i][1], &sx, &sy) &&
                sx <= screen_max_x && sy <= screen_max_y) {
                inside++;
            }
        }
        CHECK(inside == 3,
              "the three readings measured on the device land on the screen");
    }

    /* Whatever the handedness turns out to be, every point on the panel has to
     * map onto the screen and no two may collide. Sweeping the whole panel is
     * 76800 points, which costs nothing here and would catch an off-by-one in
     * either subtraction that the corners alone could miss. */
    {
        static char seen[CATNIP_SCREEN_W * CATNIP_SCREEN_H];
        int mapped = 0, out_of_range = 0, collided = 0;

        for (px = 0; px < CATNIP_LCD_PANEL_W; px++) {
            for (py = 0; py < CATNIP_LCD_PANEL_H; py++) {
                if (!catnip_touch_panel_to_screen(px, py, &sx, &sy)) continue;
                mapped++;
                if (sx > screen_max_x || sy > screen_max_y) {
                    out_of_range++;
                    continue;
                }
                if (seen[sy * CATNIP_SCREEN_W + sx]) collided++;
                seen[sy * CATNIP_SCREEN_W + sx] = 1;
            }
        }
        CHECK(mapped == CATNIP_LCD_PANEL_W * CATNIP_LCD_PANEL_H,
              "every point on the panel is accepted");
        CHECK(out_of_range == 0, "and every one of them lands on the screen");
        CHECK(collided == 0, "each screen pixel is reachable from exactly one of them");
    }

    /* Off the panel is not a point. The registers are read one byte at a time
     * over a bus shared with the PMIC and the expander, and the probe already
     * saw a reading land outside the panel, so the driver has to be told to
     * drop the sample rather than handed a clamped guess. */
    CHECK(!catnip_touch_panel_to_screen(CATNIP_LCD_PANEL_W, 0, &sx, &sy),
          "a panel X one past the edge is rejected");
    CHECK(!catnip_touch_panel_to_screen(0, CATNIP_LCD_PANEL_H, &sx, &sy),
          "a panel Y one past the edge is rejected");
    CHECK(!catnip_touch_panel_to_screen(0x0FFF, 0x0FFF, &sx, &sy),
          "and so is the all-ones reading a failed register read would give");

    /* A rejected point must leave the caller's variables alone, so that a
     * caller which ignores the return value keeps its last good position
     * instead of jumping to whatever the noise decoded to. */
    sx = 12;
    sy = 34;
    catnip_touch_panel_to_screen(0x0FFF, 0x0FFF, &sx, &sy);
    CHECK(sx == 12 && sy == 34, "a rejected reading writes nothing");

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
