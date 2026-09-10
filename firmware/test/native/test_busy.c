/*
 * Native test for the busy ring's arithmetic.
 *
 * The shape is drawn two ways - straight onto the panel at boot, and through
 * LVGL once it is up - so the arithmetic is the one thing the two have to agree
 * about, and it is the one thing a picture cannot be trusted for: a ring that
 * turns the wrong way or a tail that lights the wrong end both look like a
 * spinner from across the room.
 */
#include <stdio.h>

#include "catnip_busy.h"

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

int main(void)
{
    catnip_busy_dot d[CATNIP_BUSY_DOTS];
    int n;

    printf("the ring turns at the clock's rate, not the loop's\n");
    CHECK(catnip_busy_phase(0) == 0, "it starts at the top");
    CHECK(catnip_busy_phase(CATNIP_BUSY_STEP_MS - 1) == 0, "and holds for one step");
    CHECK(catnip_busy_phase(CATNIP_BUSY_STEP_MS) == 1, "then moves on");
    CHECK(catnip_busy_phase(CATNIP_BUSY_STEP_MS * CATNIP_BUSY_DOTS) == 0,
          "and comes round after eight of them");
    /* One revolution a second: slow enough not to look panicked, fast enough
     * not to look stuck. */
    CHECK(CATNIP_BUSY_STEP_MS * CATNIP_BUSY_DOTS == 1000u, "which is once a second");

    printf("eight dots on a ring, the first at the top, running clockwise\n");
    n = catnip_busy_dots(0, 100, 100, 20, d, CATNIP_BUSY_DOTS);
    CHECK(n == CATNIP_BUSY_DOTS, "all eight are placed");
    CHECK(d[0].x == 100 && d[0].y == 80, "the first is directly above the centre");
    CHECK(d[2].x == 120 && d[2].y == 100, "the third is directly to the right");
    CHECK(d[4].x == 100 && d[4].y == 120, "the fifth is directly below");
    CHECK(d[6].x == 80 && d[6].y == 100, "and the seventh directly to the left");

    printf("the bright one is the one the ring is moving towards\n");
    CHECK(d[0].level == CATNIP_BUSY_DOTS - 1, "at phase 0 the top dot leads");
    /* The trail is the dots it has already passed, which going clockwise from
     * the top means the one to its left - not the one it is about to reach. A
     * ring lit the other way round reads as turning backwards. */
    CHECK(d[7].level == CATNIP_BUSY_DOTS - 2, "the one it just left is next brightest");
    CHECK(d[1].level == 0, "and the one it has not reached yet is the faintest");

    catnip_busy_dots(3, 100, 100, 20, d, CATNIP_BUSY_DOTS);
    CHECK(d[3].level == CATNIP_BUSY_DOTS - 1, "a step on, and the lead has moved on");
    CHECK(d[2].level == CATNIP_BUSY_DOTS - 2, "with the tail behind it, not ahead");

    printf("it fits whatever it is given\n");
    CHECK(catnip_busy_dots(0, 0, 0, 10, d, 3) == 3,
          "a smaller buffer is filled, not run past");
    CHECK(catnip_busy_dots(0, 0, 0, 10, NULL, 8) == 0, "and no buffer is no dots");

    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
