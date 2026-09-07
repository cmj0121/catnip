/* Native test for issue #35: the names device.button() accepts.
 *
 * The case that matters is the last one. A name this map rejects reaches Lua as
 * `false`, which is exactly what a switch nobody is holding reads as, so a
 * rejected spelling is invisible from inside a script. Every spelling an app
 * author is likely to write therefore has to be pinned here rather than left to
 * be discovered on a device. */
#include <stdio.h>

#include "device/input_names.h"

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
    CHECK(catnip_button_from_name("up") == CATNIP_BTN_UP, "up");
    CHECK(catnip_button_from_name("down") == CATNIP_BTN_DOWN, "down");
    CHECK(catnip_button_from_name("left") == CATNIP_BTN_LEFT, "left");
    CHECK(catnip_button_from_name("right") == CATNIP_BTN_RIGHT, "right");
    CHECK(catnip_button_from_name("centre") == CATNIP_BTN_CENTRE, "centre");
    CHECK(catnip_button_from_name("A") == CATNIP_BTN_A, "A");
    CHECK(catnip_button_from_name("B") == CATNIP_BTN_B, "B");

    /* The enum says CENTRE and so does the rest of this codebase, but an app
     * author typing the other spelling should not get a button that is never
     * held. */
    CHECK(catnip_button_from_name("center") == CATNIP_BTN_CENTRE, "center spells centre");

    /* device.button('a') and device.button('A') are one switch. */
    CHECK(catnip_button_from_name("a") == CATNIP_BTN_A, "a is A");
    CHECK(catnip_button_from_name("UP") == CATNIP_BTN_UP, "UP is up");

    /* A prefix is not a name: "u" must not find "up", or a typo would steer the
     * joystick. */
    CHECK(catnip_button_from_name("u") == CATNIP_BTN_COUNT, "a prefix is not a switch");
    CHECK(catnip_button_from_name("upward") == CATNIP_BTN_COUNT,
          "a longer word is not a switch");
    CHECK(catnip_button_from_name("") == CATNIP_BTN_COUNT, "the empty name is nothing");
    CHECK(catnip_button_from_name("X") == CATNIP_BTN_COUNT, "an unknown name is nothing");
    CHECK(catnip_button_from_name(0) == CATNIP_BTN_COUNT, "NULL is nothing");

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
