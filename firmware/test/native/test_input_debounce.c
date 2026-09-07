/* Native test for issue #42: the bounce filter behind the button driver.
 *
 * The interesting case is not invented. It is the trace the probe logged from
 * one real press of the MeowKit switch on GPIO4 - button B, as the probe's
 * later press tallies established: down at 255966, up at 256792,
 * down again 30 ms later at 256822, and up at 256982. Those middle two edges
 * are the contact bouncing, and a driver that reports them has invented a
 * second press the human never made. The clock here is a plain variable, so
 * the whole filter runs on the host with no device attached. */
#include <stdio.h>

#include "device/board.h"
#include "device/input_debounce.h"

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

/* Poll the filter the way the main loop does - every millisecond from `from`
 * up to and including `to`, with the raw level held at `down` throughout. */
static void hold(catnip_debounce *d, bool down, uint32_t from, uint32_t to)
{
    uint32_t t;

    for (t = from; t <= to; t++) {
        catnip_debounce_update(d, down, t);
    }
}

int main(void)
{
    catnip_debounce d;
    int presses, releases;
    uint32_t t;

    printf("== input debounce ==\n");

    /* The measured press of A, replayed. */
    catnip_debounce_init(&d, 255000);
    presses = releases = 0;

    hold(&d, false, 255000, 255965);
    CHECK(!catnip_debounce_down(&d), "starts up");

    hold(&d, true, 255966, 256791);
    presses += catnip_debounce_take_pressed(&d) ? 1 : 0;
    CHECK(catnip_debounce_down(&d), "the press is seen");

    hold(&d, false, 256792, 256821); /* released */
    hold(&d, true, 256822, 256981);  /* and bouncing back closed */
    releases += catnip_debounce_take_released(&d) ? 1 : 0;
    CHECK(releases == 0,
          "the release is not reported while the contact is still bouncing");

    hold(&d, false, 256982, 257200);
    presses += catnip_debounce_take_pressed(&d) ? 1 : 0;
    releases += catnip_debounce_take_released(&d) ? 1 : 0;

    CHECK(presses == 1, "one press, not two - the bounce is filtered out");
    CHECK(releases == 1, "and one release, once the switch is finally open");
    CHECK(!catnip_debounce_down(&d), "ends up");

    /* An edge costs the window in latency, and nothing more than that. */
    catnip_debounce_init(&d, 1000);
    hold(&d, true, 1100, 1100 + CATNIP_INPUT_DEBOUNCE_MS - 1);
    CHECK(!catnip_debounce_down(&d), "a press is not believed before it has settled");
    catnip_debounce_update(&d, true, 1100 + CATNIP_INPUT_DEBOUNCE_MS);
    CHECK(catnip_debounce_take_pressed(&d), "and is believed the moment it has");

    /* The shortest press that was measured on the device - 125 ms - survives
     * the filter intact, which is what makes the window safe to use. */
    catnip_debounce_init(&d, 0);
    hold(&d, true, 2000, 2125);
    hold(&d, false, 2126, 2400);
    CHECK(catnip_debounce_take_pressed(&d), "a 125 ms press is not swallowed");
    CHECK(catnip_debounce_take_released(&d), "and it is released again");

    /* A switch that never settles is never reported. */
    catnip_debounce_init(&d, 0);
    for (t = 3000; t < 3600; t += 2) {
        catnip_debounce_update(&d, true, t);
        catnip_debounce_update(&d, false, t + 1);
    }
    CHECK(!catnip_debounce_take_pressed(&d), "a chattering contact reports nothing");
    CHECK(!catnip_debounce_down(&d), "and leaves the switch up");

    /* An edge is delivered to exactly one caller. */
    catnip_debounce_init(&d, 0);
    hold(&d, true, 4000, 4000 + CATNIP_INPUT_DEBOUNCE_MS);
    CHECK(catnip_debounce_take_pressed(&d), "the press edge reads true once");
    CHECK(!catnip_debounce_take_pressed(&d), "and false the second time");
    CHECK(catnip_debounce_down(&d), "while the switch itself stays down");

    /* millis() wraps every 49 days, and the filter has to keep working across
     * it: the elapsed time is an unsigned difference, not a comparison. */
    t = 0xFFFFFFFFu - 10u;
    catnip_debounce_init(&d, t);
    catnip_debounce_update(&d, true, t);
    catnip_debounce_update(&d, true, t + CATNIP_INPUT_DEBOUNCE_MS); /* wrapped */
    CHECK(catnip_debounce_take_pressed(&d), "a press that settles across the clock wrap");

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
