/*
 * Short and long out of one contact.
 *
 * These are the cases a finger cannot check for you: that the long fires while
 * the button is still down and not on release, that the release which follows
 * it is silent, and that a press held exactly to the threshold counts.
 */
#include <stdio.h>

#include "device/press_gesture.h"

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

/* Hold the switch from `t0` for `ms`, sampling every 10 ms as the main loop
 * does, and report what came out and when. */
static void hold(catnip_press *p, unsigned t0, unsigned ms, int *ev, unsigned *at)
{
    *ev = CATNIP_PRESS_NONE;
    *at = 0;
    for (unsigned t = t0; t <= t0 + ms; t += 10) {
        int e = catnip_press_step(p, true, t);
        if (e != CATNIP_PRESS_NONE && *ev == CATNIP_PRESS_NONE) {
            *ev = e;
            *at = t - t0;
        }
    }
}

int main(void)
{
    catnip_press p = {0};
    int ev;
    unsigned at;

    printf("a quick press is short\n");
    CHECK(catnip_press_step(&p, true, 1000) == CATNIP_PRESS_NONE,
          "going down decides nothing yet");
    CHECK(catnip_press_step(&p, true, 1100) == CATNIP_PRESS_NONE, "nor does holding");
    CHECK(catnip_press_step(&p, false, 1200) == CATNIP_PRESS_SHORT,
          "the release before the threshold is a short press");
    CHECK(catnip_press_step(&p, false, 1300) == CATNIP_PRESS_NONE,
          "and staying up says nothing more");

    printf("a held press is long, and it fires while it is still held\n");
    p = (catnip_press){0};
    hold(&p, 5000, 1000, &ev, &at);
    CHECK(ev == CATNIP_PRESS_LONG, "holding past the threshold is a long press");
    CHECK(at >= CATNIP_LONG_PRESS_MS && at < CATNIP_LONG_PRESS_MS + 20,
          "and it arrives at the threshold, not on release");
    CHECK(catnip_press_step(&p, true, 7000) == CATNIP_PRESS_NONE,
          "holding longer does not fire it twice");
    CHECK(catnip_press_step(&p, false, 8000) == CATNIP_PRESS_NONE,
          "the release after a long press is silent");

    printf("the two never both happen\n");
    /* The bug this pins: emitting the long at the threshold and the short on
     * release would make every long press a long *and* a short - long B would
     * go home and then B would also go back, from the one gesture. */
    p = (catnip_press){0};
    hold(&p, 0, CATNIP_LONG_PRESS_MS + 100, &ev, &at);
    CHECK(ev == CATNIP_PRESS_LONG, "held long enough: long");
    CHECK(catnip_press_step(&p, false, CATNIP_LONG_PRESS_MS + 200) == CATNIP_PRESS_NONE,
          "and no short follows it");

    printf("the threshold itself counts as long\n");
    p = (catnip_press){0};
    catnip_press_step(&p, true, 100);
    CHECK(catnip_press_step(&p, true, 100 + CATNIP_LONG_PRESS_MS) == CATNIP_PRESS_LONG,
          "exactly LONG_PRESS_MS is long, not short");
    p = (catnip_press){0};
    catnip_press_step(&p, true, 100);
    CHECK(catnip_press_step(&p, false, 100 + CATNIP_LONG_PRESS_MS - 1) ==
              CATNIP_PRESS_SHORT,
          "one millisecond short of it is short");

    printf("a switch already down when we start over is not a long press\n");
    /* A user who holds B to go home lands somewhere new with B still down. If
     * that contact carried its start time across, the new screen would take a
     * long press the user never made there. */
    p = (catnip_press){0};
    catnip_press_step(&p, true, 20000);
    CHECK(catnip_press_step(&p, true, 20100) == CATNIP_PRESS_NONE,
          "its contact starts when we first see it");

    printf("two presses in a row are two presses\n");
    p = (catnip_press){0};
    catnip_press_step(&p, true, 100);
    CHECK(catnip_press_step(&p, false, 200) == CATNIP_PRESS_SHORT, "the first is short");
    catnip_press_step(&p, true, 300);
    hold(&p, 300, 800, &ev, &at);
    CHECK(ev == CATNIP_PRESS_LONG, "and the second can still be long");

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
