/*
 * Native test for issue #67: the preference page's two states and its ladders.
 *
 * Drawing the page is LVGL and needs the device; the part everything else
 * depends on is plain. The page opens with nothing chosen, left and right move
 * the ring, A or a tap makes a column live, and only then do up and down change
 * anything. That second state is the whole safety argument of the page: arriving
 * on five settings with one of them already under the joystick is how a
 * brightness gets changed by someone who only wanted to look.
 *
 * Two kinds of column share the page. Screen is a continuous range, because
 * every brightness between the ends is a real one; the rest are ladders of a
 * few named rungs, because there are four breathing rhythms worth having and
 * not eight hundred. Both clamp rather than wrap, so the floors are floors: the
 * dimmest screen is one that can still be read, and no sequence of presses
 * reaches a dark panel whose way back is drawn on itself.
 */
#include <stdio.h>
#include <string.h>

#include "catnip_config.h"
#include "catnip_runtime.h"
#include "catnip_settings.h"
#include "catnip_ui.h"

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

static void fire(catnip_rt *rt, const char *lua)
{
    catnip_rt_dostring(rt, lua, "=t");
}

int main(void)
{
    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_ui_open(rt);
    catnip_settings *s = catnip_settings_new(rt);
    catnip_config cfg;
    const catnip_config *now;

    printf("the preference page chooses, then changes\n");
    CHECK(s != NULL, "the page is created");

    catnip_config_defaults(&cfg);
    catnip_settings_show(s, &cfg);
    now = catnip_settings_config(s);
    CHECK(now != NULL && now->screen_brightness == cfg.screen_brightness,
          "showing it holds the settings it was given");
    CHECK(!catnip_settings_take_dirty(s), "and nothing is dirty before a press");

    /* Nothing is chosen yet, so up and down are not the value of anything. */
    fire(rt, "ui.fire('settings_list', 'lower')");
    fire(rt, "ui.fire('settings_list', 'raise')");
    CHECK(!catnip_settings_take_dirty(s),
          "up and down do nothing until a column is live");
    CHECK(catnip_settings_config(s)->screen_brightness == 100, "and change nothing");

    /* Right lands on the first column; A makes it live; only now does down move
     * the value. */
    fire(rt, "ui.fire('settings_list', 'next')");
    fire(rt, "ui.fire('settings_list', 'lower')");
    CHECK(catnip_settings_config(s)->screen_brightness == 100,
          "the ring alone does not make a column live");
    fire(rt, "ui.fire('settings_list', 'click')");
    fire(rt, "ui.fire('settings_list', 'lower')");
    now = catnip_settings_config(s);
    CHECK(now->screen_brightness == 95, "A makes it live and down steps it");
    CHECK(catnip_settings_take_dirty(s), "a change marks the page dirty");
    CHECK(!catnip_settings_take_dirty(s), "and reading that clears it");

    /* The floor is a floor: no number of presses reaches a dark screen. */
    for (int i = 0; i < 40; i++)
        fire(rt, "ui.fire('settings_list', 'lower')");
    now = catnip_settings_config(s);
    CHECK(now->screen_brightness == CATNIP_SCREEN_MIN_PCT,
          "down stops at the dimmest screen that can still be read");
    CHECK(now->screen_brightness > 0, "and never at one that is off");

    /* And the ceiling is a ceiling. */
    for (int i = 0; i < 40; i++)
        fire(rt, "ui.fire('settings_list', 'raise')");
    CHECK(catnip_settings_config(s)->screen_brightness == 100, "up stops at the top");

    /* B lets go of the column before it leaves the page: it is claimed while
     * something is live and declined when nothing is, which is what makes one
     * press per level rather than one press out of everything. */
    fire(rt, "ui.fire('settings_screen', 'back')");
    fire(rt, "ui.fire('settings_list', 'lower')");
    CHECK(catnip_settings_config(s)->screen_brightness == 100,
          "B lets go of the column, and down stops meaning anything");

    /* Moving the ring is independent of the values. */
    catnip_settings_show(s, &cfg);
    fire(rt, "ui.fire('settings_list', 'next')");
    fire(rt, "ui.fire('settings_list', 'next')");
    fire(rt, "ui.fire('settings_list', 'click')");
    fire(rt, "ui.fire('settings_list', 'lower')");
    now = catnip_settings_config(s);
    CHECK(now->screen_brightness == 100, "moving on leaves the last column alone");
    CHECK(now->idle_off_s == 30, "and the new column is the one that moved");

    /* Left off the end stays on the first column rather than coming round. */
    for (int i = 0; i < 5; i++)
        fire(rt, "ui.fire('settings_list', 'prev')");
    fire(rt, "ui.fire('settings_list', 'click')");
    fire(rt, "ui.fire('settings_list', 'lower')");
    CHECK(catnip_settings_config(s)->screen_brightness == 95,
          "left off the end stays on the first column");

    /* A tap names its own column, where the joystick can only have moved to
     * one - and it makes it live in the same press. */
    catnip_settings_show(s, &cfg);
    fire(rt, "ui.fire('settings_list', 'click', 3)");
    for (int i = 0; i < 4; i++)
        fire(rt, "ui.fire('settings_list', 'lower')");
    now = catnip_settings_config(s);
    /* The owner may turn the LED off. An app may not - see led.h - because a
     * dark LED and a dead board look alike, and that rule exists so nobody is
     * misled. Nobody is misled by their own decision. */
    CHECK(now->led_brightness == 0, "a tap makes its column live, and Off is reachable");

    /* A finger dragged up a column puts it where the finger is, rather than one
     * step further on: the drag names a place, not a direction. */
    catnip_settings_show(s, &cfg);
    fire(rt, "ui.fire('set1', 'drag', 0)");
    CHECK(catnip_settings_config(s)->screen_brightness == CATNIP_SCREEN_MIN_PCT,
          "dragging to the bottom of a column asks for its lowest rung");
    fire(rt, "ui.fire('set1', 'drag', 50)");
    CHECK(catnip_settings_config(s)->screen_brightness == 60,
          "and halfway up asks for the middle one");

    /* The two kinds of column really are different: one press of down moves the
     * range by its increment and the ladder by a whole rung. */
    catnip_settings_show(s, &cfg);
    fire(rt, "ui.fire('settings_list', 'click', 1)");
    fire(rt, "ui.fire('settings_list', 'lower')");
    fire(rt, "ui.fire('settings_list', 'click', 4)");
    fire(rt, "ui.fire('settings_list', 'lower')");
    now = catnip_settings_config(s);
    CHECK(now->screen_brightness == 95, "a range steps by its increment");
    /* And down on Breath is a shorter breath, not a slower rate: the bar and
     * the number it prints have to move together, so the rungs run fastest at
     * the bottom. 0.4 breaths a second is a 2.5 s breath; one rung down is
     * 0.8, which is 1.2 s. */
    CHECK(now->led_breaths_per_second > 0.79f && now->led_breaths_per_second < 0.81f,
          "and a ladder steps by a whole rung, downward being the shorter breath");

    /* A value that is not on the ladder - a hand-edited file, or a ladder that
     * changed between firmwares - snaps to the nearest rung rather than being
     * refused, and what is shown is then what is held. */
    catnip_config_defaults(&cfg);
    cfg.screen_brightness = 57;
    cfg.idle_off_s = 3600;
    catnip_settings_show(s, &cfg);
    now = catnip_settings_config(s);
    CHECK(now->screen_brightness == 55,
          "a range value snaps to the nearest step it could have been set to");
    CHECK(now->idle_off_s == 300, "and one past the top snaps to the top");

    catnip_settings_free(s);
    catnip_rt_free(rt);
    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
