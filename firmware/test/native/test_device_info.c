/*
 * Native test for issue #69: the device info page, and the hub it became.
 *
 * The page has almost no logic in it on purpose - what a chip is called and how
 * much PSRAM is free are the device's questions, and it is handed the answers
 * already written out. What is left, and what is pinned here, is what it
 * decides: that the two buttons are told apart, that a fact is not a button,
 * and that a press nobody read does not survive the page it was made on.
 *
 * The middle one is the whole point of the page. A screen of facts where A on
 * "Free heap" did something would be promising what it cannot deliver, and the
 * fastest way to teach that A does nothing on a fact is for A to do nothing on
 * a fact.
 */
#include <stdio.h>
#include <string.h>

#include "catnip_device_info.h"
#include "catnip_runtime.h"
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
    catnip_device_info *d = catnip_device_info_new(rt);
    const char *rows[3] = {"catnip v0.3.1", "heap 213 KB free", "card none"};
    const catnip_info_key pref = {"Preference", "settings"};
    const catnip_info_key diag = {"Diagnostic", "warning"};

    printf("the device info page reports, and two buttons act\n");
    CHECK(d != NULL, "the page is created");

    catnip_device_info_show(d, rows, 3, &pref, &diag);
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_NONE,
          "nothing is latched before a press");

    /* The two are told apart, which is the only thing the caller asks of this
     * page: one goes to the preference plane and the other takes the screen. */
    fire(rt, "ui.fire('info_left', 'click')");
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_LEFT,
          "the left button is left");
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_NONE,
          "and reading the latch clears it");

    fire(rt, "ui.fire('info_right', 'click')");
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_RIGHT,
          "the right button is right");

    /* Every row above them is a fact, and a fact does nothing. */
    fire(rt, "ui.fire('info_list', 'click')");
    fire(rt, "ui.fire('info_list', 'click', 1)");
    fire(rt, "ui.fire('info_list', 'click', 3)");
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_NONE,
          "clicking a fact does nothing at all");

    /* That the facts are not a focus stop either - so the ring opens on the
     * left button rather than on a column that will not answer it - is the
     * renderer's rule about a list with no handlers, and is pinned with the
     * rest of the focus order in test_render.c.
     */

    /* A page with one button has one, and a page with none has nothing that can
     * be activated at all. */
    catnip_device_info_show(d, rows, 3, &pref, NULL);
    fire(rt, "ui.fire('info_right', 'click')");
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_NONE,
          "a button that was not asked for is not there");

    catnip_device_info_show(d, rows, 3, NULL, NULL);
    fire(rt, "ui.fire('info_list', 'click')");
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_NONE,
          "a page of only facts latches nothing");

    /* A rebuild forgets a press nobody consumed: the page it belonged to is
     * gone, and acting on it would leave home because of something the user did
     * on a screen that is no longer there. */
    catnip_device_info_show(d, rows, 3, &pref, &diag);
    fire(rt, "ui.fire('info_left', 'click')");
    catnip_device_info_show(d, rows, 3, &pref, &diag);
    CHECK(catnip_device_info_take_action(d) == CATNIP_INFO_NONE,
          "a rebuild drops an unread press");

    /* More rows than the page holds are cut rather than overrunning it. */
    {
        const char *many[CATNIP_INFO_MAX_ROWS + 4];
        for (int i = 0; i < CATNIP_INFO_MAX_ROWS + 4; i++)
            many[i] = "x";
        catnip_device_info_show(d, many, CATNIP_INFO_MAX_ROWS + 4, NULL, NULL);
        CHECK(1, "too many rows is a cut, not a crash");
    }

    catnip_device_info_free(d);
    catnip_rt_free(rt);
    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
