/*
 * Native test for issue #31: the switch-to-event convention.
 *
 * The wiring that reads the switches and drives LVGL is device code and cannot
 * run here, but the convention it delivers - which switch posts which event,
 * and how the focus cursor walks the renderer's focus order - is plain C and is
 * exactly what an app is written against. A rename here is the File Browser
 * breaking silently, so the names are pinned in a test rather than only in a
 * comment.
 */
#include <stdio.h>

#include "device/ui_input_map.h"

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

static int streq(const char *a, const char *b)
{
    if (!a || !b) return a == b;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void test_event_names(void)
{
    printf("the switches post the events apps listen for\n");
    CHECK(streq(catnip_ui_input_event(CATNIP_BTN_UP), "prev"),
          "UP moves a list's selection back (on_prev)");
    CHECK(streq(catnip_ui_input_event(CATNIP_BTN_DOWN), "next"),
          "DOWN moves it forward (on_next)");
    CHECK(streq(catnip_ui_input_event(CATNIP_BTN_A), "click"), "A activates (on_click)");
    CHECK(streq(catnip_ui_input_event(CATNIP_BTN_CENTRE), "click"),
          "the joystick centre also activates, so A is not the only way (#49)");
    CHECK(catnip_ui_input_event(CATNIP_BTN_LEFT) == NULL,
          "LEFT posts nothing - it moves focus");
    CHECK(catnip_ui_input_event(CATNIP_BTN_RIGHT) == NULL,
          "RIGHT posts nothing - it moves focus");
    CHECK(catnip_ui_input_event(CATNIP_BTN_B) == NULL, "B posts nothing - it goes back");
}

static void test_focus_dir(void)
{
    printf("LEFT and RIGHT move the focus between focusables\n");
    CHECK(catnip_ui_input_focus_dir(CATNIP_BTN_LEFT) == -1, "LEFT is the previous");
    CHECK(catnip_ui_input_focus_dir(CATNIP_BTN_RIGHT) == 1, "RIGHT is the next");
    CHECK(catnip_ui_input_focus_dir(CATNIP_BTN_UP) == 0, "UP does not move focus");
    CHECK(catnip_ui_input_focus_dir(CATNIP_BTN_A) == 0, "A does not move focus");
}

static void test_focus_step(void)
{
    printf("the focus cursor walks the renderer's focus order\n");
    catnip_handle order[3] = {10, 20, 30};

    CHECK(catnip_ui_focus_step(order, 0, 10, 1) == CATNIP_HANDLE_NONE,
          "an empty order has nothing to focus");
    CHECK(catnip_ui_focus_step(order, 3, CATNIP_HANDLE_NONE, 1) == 10,
          "nothing focused yet lands on the first entry");
    CHECK(catnip_ui_focus_step(order, 3, 99, -1) == 10,
          "a handle no longer in the order restarts at the first entry");
    CHECK(catnip_ui_focus_step(order, 3, 10, 1) == 20, "RIGHT steps forward");
    CHECK(catnip_ui_focus_step(order, 3, 20, -1) == 10, "LEFT steps back");
    CHECK(catnip_ui_focus_step(order, 3, 10, -1) == 10, "it clamps at the top");
    CHECK(catnip_ui_focus_step(order, 3, 30, 1) == 30, "and clamps at the bottom");
    CHECK(catnip_ui_focus_step(order, 3, 20, 0) == 20,
          "a zero move validates the focus without moving it");
}

int main(void)
{
    test_event_names();
    test_focus_dir();
    test_focus_step();
    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
