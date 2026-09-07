/* Native test for issue #35: turning a cell voltage into device.battery().
 *
 * The assertions that matter are the ones about -1. The AXP173's battery ADC
 * registers have not been confirmed on this board, so the gauge has to be able
 * to say "that is not a battery" rather than clamp a wrong register into a
 * plausible percentage - and a refusal nobody has ever seen fire is the kind of
 * thing that gets simplified away. These hold it in place. */
#include <stdio.h>

#include "device/battery_gauge.h"

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
    CHECK(catnip_battery_percent_from_mv(3300) == 0, "3.30 V is empty");
    CHECK(catnip_battery_percent_from_mv(4200) == 100, "4.20 V is full");
    CHECK(catnip_battery_percent_from_mv(3750) == 50, "3.75 V is halfway");

    /* A cell under load sags and one off the charger reads high; both are still
     * cells, so both are clamped rather than refused. */
    CHECK(catnip_battery_percent_from_mv(3000) == 0, "a sagging cell is 0, not unknown");
    CHECK(catnip_battery_percent_from_mv(4300) == 100,
          "a freshly charged cell is 100, not unknown");

    /* Outside the band the reading is not a battery, it is a wrong register. */
    CHECK(catnip_battery_percent_from_mv(0) == -1, "zero is not a battery");
    CHECK(catnip_battery_percent_from_mv(2499) == -1, "below the band is unknown");
    CHECK(catnip_battery_percent_from_mv(4501) == -1, "above the band is unknown");

    /* The case this band was drawn for. The ADC is 12 bits at 1.1 mV a count,
     * so a register stuck at full scale decodes to 4095 * 11 / 10 = 4504 mV.
     * That has to read as unknown: clamped, it would be a device reporting a
     * confident 100% forever while the battery ran flat. */
    CHECK(catnip_battery_percent_from_mv(4095u * 11u / 10u) == -1,
          "an ADC stuck at full scale is unknown, not 100%");

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
