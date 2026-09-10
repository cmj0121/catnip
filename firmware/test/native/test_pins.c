/*
 * Native test for the pinned-apps set (#71).
 *
 * The set is a comma-separated string, and the ways string sets go wrong are
 * the ways this checks: an id that is a prefix of another matching by accident,
 * a removal leaving an empty field or a stray comma, an append that overflows
 * turning the list into a different set.
 */
#include <stdio.h>
#include <string.h>

#include "catnip_pins.h"

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
    char csv[64] = "";

    printf("the pinned-apps set\n");

    CHECK(!catnip_pins_contains(csv, "clock"), "an empty set contains nothing");
    CHECK(!catnip_pins_contains("", "clock"), "and neither does the empty string");

    catnip_pins_toggle(csv, sizeof(csv), "clock");
    CHECK(strcmp(csv, "clock") == 0, "the first id is the whole string");
    CHECK(catnip_pins_contains(csv, "clock"), "and is found");
    /* A prefix must not match. */
    CHECK(!catnip_pins_contains(csv, "clockwork"), "a longer id is not a match");
    CHECK(!catnip_pins_contains("clockwork", "clock"), "and a prefix is not either");

    catnip_pins_toggle(csv, sizeof(csv), "dino");
    CHECK(strcmp(csv, "clock,dino") == 0, "a second id is appended with a comma");
    CHECK(catnip_pins_contains(csv, "dino") && catnip_pins_contains(csv, "clock"),
          "and both are found");

    /* Remove the middle-ish (first) field: no empty field, no leading comma. */
    catnip_pins_toggle(csv, sizeof(csv), "clock");
    CHECK(strcmp(csv, "dino") == 0, "removing the first field leaves no comma");
    /* Remove the last remaining field: empty string, not a stray comma. */
    catnip_pins_toggle(csv, sizeof(csv), "dino");
    CHECK(csv[0] == '\0', "removing the last field empties the string cleanly");

    /* Toggle is add-or-remove. */
    catnip_pins_toggle(csv, sizeof(csv), "a");
    catnip_pins_toggle(csv, sizeof(csv), "b");
    catnip_pins_toggle(csv, sizeof(csv), "c");
    CHECK(strcmp(csv, "a,b,c") == 0, "three ids");
    catnip_pins_toggle(csv, sizeof(csv), "b");
    CHECK(strcmp(csv, "a,c") == 0, "removing a middle field closes the gap");

    /* An append that will not fit is dropped whole, not truncated. */
    {
        char small[8] = "ab";
        CHECK(!catnip_pins_toggle(small, sizeof(small), "longid"),
              "an id that would overflow is refused");
        CHECK(strcmp(small, "ab") == 0, "and the set is left exactly as it was");
    }

    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
