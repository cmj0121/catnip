/*
 * A finger drag, read as a direction.
 *
 * The cases a finger cannot check for you: that a still finger's jitter is not
 * a swipe, that a long drag keeps stepping at an even cost, and that the
 * release which ends a swipe is not also a tap.
 */
#include <stdio.h>

#include "device/swipe.h"

static int failures;

#define CHECK(cond, what)                                                                \
    do {                                                                                 \
        if (cond) {                                                                      \
            printf("  ok   - %s\n", what);                                               \
        } else {                                                                         \
            printf("  FAIL - %s\n", what);                                               \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

/* Drag from (x0,y0) by (dx,dy) in `steps` samples, counting what came out. */
static int drag(catnip_swipe *s, int x0, int y0, int dx, int dy, int steps, int *dir)
{
    int fired = 0;
    *dir = CATNIP_SWIPE_NONE;
    catnip_swipe_step(s, true, x0, y0);
    for (int i = 1; i <= steps; i++) {
        int e = catnip_swipe_step(s, true, x0 + dx * i / steps, y0 + dy * i / steps);
        if (e != CATNIP_SWIPE_NONE) {
            fired++;
            *dir = e;
        }
    }
    return fired;
}

int main(void)
{
    catnip_swipe s = {0};
    int dir, n;

    printf("a still finger is not a swipe\n");
    CHECK(catnip_swipe_step(&s, true, 100, 100) == CATNIP_SWIPE_NONE,
          "touching down decides nothing");
    /* The panel's own tests show a resting finger wandering a few pixels. If
     * that were a swipe, a tap would move the selection before activating it. */
    CHECK(catnip_swipe_step(&s, true, 103, 97) == CATNIP_SWIPE_NONE,
          "and the jitter of a resting finger is not movement");
    CHECK(catnip_swipe_moved(&s) == false, "so the contact is still a tap");
    catnip_swipe_step(&s, false, 103, 97);

    printf("a drag is the direction it mostly went\n");
    s = (catnip_swipe){0};
    n = drag(&s, 160, 120, 0, -60, 6, &dir);
    CHECK(dir == CATNIP_SWIPE_UP, "up the panel is up");
    CHECK(n == 1, "and however far it goes, one drag is one step");

    s = (catnip_swipe){0};
    (void)drag(&s, 160, 120, 0, 60, 6, &dir);
    CHECK(dir == CATNIP_SWIPE_DOWN, "down is down");
    s = (catnip_swipe){0};
    (void)drag(&s, 160, 120, -60, 0, 6, &dir);
    CHECK(dir == CATNIP_SWIPE_LEFT, "left is left");
    s = (catnip_swipe){0};
    (void)drag(&s, 160, 120, 60, 0, 6, &dir);
    CHECK(dir == CATNIP_SWIPE_RIGHT, "right is right");

    printf("a crooked drag still has one direction\n");
    /* Nobody drags in a straight line. Asking whether the other axis moved at
     * all would make every real vertical drag ambiguous. */
    s = (catnip_swipe){0};
    (void)drag(&s, 160, 120, 14, -50, 5, &dir);
    CHECK(dir == CATNIP_SWIPE_UP, "mostly up, a little sideways, is up");

    printf("a long drag is still one step\n");
    /* The finger asked to move, not to keep moving. A drag that stepped once
     * per threshold sent a carousel flying past whatever was being looked at,
     * and a second step is a second swipe. */
    s = (catnip_swipe){0};
    n = drag(&s, 160, 200, 0, -120, 120, &dir);
    CHECK(n == 1, "120 pixels is one step, not five");
    CHECK(dir == CATNIP_SWIPE_UP, "and it is still the direction it went");

    printf("the release that ends a swipe is not a tap\n");
    s = (catnip_swipe){0};
    (void)drag(&s, 160, 120, 0, -60, 6, &dir);
    CHECK(catnip_swipe_moved(&s), "the contact is marked as having moved");
    CHECK(catnip_swipe_step(&s, false, 160, 60) == CATNIP_SWIPE_NONE,
          "and lifting the finger fires nothing more");
    CHECK(catnip_swipe_moved(&s) == false, "the next contact starts as a tap again");

    printf("a contact already down when we start over is not a swipe\n");
    /* A finger still on the glass when the screen changes underneath it must
     * not carry its start point into the new screen. */
    s = (catnip_swipe){0};
    catnip_swipe_step(&s, true, 10, 10);
    CHECK(catnip_swipe_step(&s, true, 12, 12) == CATNIP_SWIPE_NONE,
          "its origin is where we first saw it");

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
