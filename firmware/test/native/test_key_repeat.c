/*
 * Native test for the held-direction convention (#73).
 *
 * A held key repeats, and the two numbers that make it usable are the two this
 * pins: a step on the edge so the key answers at once, and a delay before the
 * second one so a press meant as one press cannot become two.
 *
 * The third case is the one that would be found late and blamed on the
 * hardware: the millisecond counter wraps every forty-nine days, and a
 * comparison rather than a subtraction would leave a key jammed down from that
 * moment until the device was restarted.
 */
#include <stdio.h>

#include "device/key_repeat.h"

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

/* How many steps a key held from `t0` for `ms` produces, sampled every 10 ms the
 * way the main loop does. */
static int held_for(unsigned ms)
{
    catnip_repeat r = {0};
    int steps = 0;
    for (unsigned t = 0; t <= ms; t += 10)
        if (catnip_repeat_step(&r, true, t)) steps++;
    return steps;
}

int main(void)
{
    catnip_repeat r = {0};

    printf("a held direction keeps going\n");

    CHECK(catnip_repeat_step(&r, true, 1000), "the first step is on the edge");
    CHECK(!catnip_repeat_step(&r, true, 1010), "and the next sample is not another");
    CHECK(!catnip_repeat_step(&r, true, 1000 + CATNIP_REPEAT_DELAY_MS - 10),
          "nothing repeats before the delay is served");
    CHECK(catnip_repeat_step(&r, true, 1000 + CATNIP_REPEAT_DELAY_MS),
          "and the second step comes when it is");

    /* Letting go resets, so a tap costs one step however long the last one was
     * held - otherwise a key would arrive already repeating. */
    CHECK(!catnip_repeat_step(&r, false, 2000), "letting go is not a step");
    CHECK(catnip_repeat_step(&r, true, 2010), "and the next press starts over");
    CHECK(!catnip_repeat_step(&r, true, 2020), "with its own delay to serve");

    /* A deliberate tap is one step, and a long hold is a stream. */
    CHECK(held_for(200) == 1, "a 200 ms tap is one step");
    CHECK(held_for(CATNIP_REPEAT_DELAY_MS - 20) == 1,
          "and so is one just under the delay");
    {
        int n = held_for(CATNIP_REPEAT_DELAY_MS + 10 * CATNIP_REPEAT_EVERY_MS);
        /* The edge, then ten intervals, give or take the 10 ms sampling. */
        CHECK(n >= 9 && n <= 12, "holding on gives a steady stream");
    }

    /* The counter wraps every 49 days. A key held across that must cost one
     * step, not jam. */
    {
        catnip_repeat w = {0};
        unsigned near_end = 0xFFFFFF00u;
        CHECK(catnip_repeat_step(&w, true, near_end), "a press just before the wrap");
        CHECK(!catnip_repeat_step(&w, true, near_end + 10), "does not repeat at once");
        /* 0xFFFFFF00 + 0x200 wraps past zero. */
        CHECK(catnip_repeat_step(&w, true, near_end + 0x200),
              "and repeats normally across the wrap rather than never or forever");
    }

    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
