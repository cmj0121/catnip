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
#include <stdbool.h>
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

/* A page of values, as a situation written down. */
static catnip_dir_where mixer(bool engaged, bool page_back, bool page_fwd)
{
    catnip_dir_where w = {CATNIP_LAYOUT_MIXER, 0,       false, false, engaged,
                          page_back,           page_fwd};
    return w;
}

static void test_mixer_levels(void)
{
    printf("a page of values has two levels, and they are not two modes\n");

    /* Unfocused: sideways chooses a column, up and down are the page. */
    {
        catnip_dir_where w = mixer(false, true, true);
        CHECK(catnip_ui_input_dir(CATNIP_BTN_LEFT, &w) == CATNIP_DIR_PREV,
              "left chooses the previous column");
        CHECK(catnip_ui_input_dir(CATNIP_BTN_RIGHT, &w) == CATNIP_DIR_NEXT,
              "right chooses the next one");
        CHECK(catnip_ui_input_dir(CATNIP_BTN_UP, &w) == CATNIP_DIR_PAGE_BACK,
              "up is the page before this one");
        CHECK(catnip_ui_input_dir(CATNIP_BTN_DOWN, &w) == CATNIP_DIR_PAGE_FWD,
              "and down the page after it");
    }

    /* One page: up and down are dimmed rather than lit for a page that is not
     * there. The dimming is the whole value of the hint. */
    {
        catnip_dir_where w = mixer(false, false, false);
        CHECK(catnip_ui_input_dir(CATNIP_BTN_UP, &w) == CATNIP_DIR_NOTHING,
              "a page with no page before it dims up");
        CHECK(catnip_ui_input_dir(CATNIP_BTN_DOWN, &w) == CATNIP_DIR_NOTHING,
              "and none after it dims down");
    }

    /* Focused: up and down are the value, and sideways goes dead. That is the
     * one thing the level buys - a sideways push that landed on the neighbour
     * would move a number nobody is looking at. */
    {
        catnip_dir_where w = mixer(true, true, true);
        CHECK(catnip_ui_input_dir(CATNIP_BTN_UP, &w) == CATNIP_DIR_RAISE,
              "up is the value of the column you are on");
        CHECK(catnip_ui_input_dir(CATNIP_BTN_DOWN, &w) == CATNIP_DIR_LOWER,
              "and down is the same value the other way");
        CHECK(catnip_ui_input_dir(CATNIP_BTN_LEFT, &w) == CATNIP_DIR_NOTHING,
              "left does nothing while a value is being changed");
        CHECK(catnip_ui_input_dir(CATNIP_BTN_RIGHT, &w) == CATNIP_DIR_NOTHING,
              "and so does right");
    }

    /* Engaged is a claim about a page of values and nothing else: it cannot
     * leak into a shape that has no columns to be on. */
    {
        catnip_dir_where w = {CATNIP_LAYOUT_GRID, 0, false, false, true, false, false};
        CHECK(catnip_ui_input_dir(CATNIP_BTN_LEFT, &w) == CATNIP_DIR_PREV,
              "a grid steps its selection whatever the level says");
    }
}

int main(void)
{
    test_event_names();
    test_focus_dir();
    test_focus_step();
    test_mixer_levels();
    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
