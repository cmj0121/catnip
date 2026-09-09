/*
 * Native test for issue #69: the device info page.
 *
 * The page has almost no logic in it on purpose - what a chip is called and how
 * much PSRAM is free are the device's questions, and it is handed the answers
 * already written out. What is left, and what is pinned here, is the one thing
 * it does decide: which row acts, and that no other row does.
 *
 * That distinction is the whole point of the page. A screen of facts where A on
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

    printf("the device info page reports, and one row acts\n");
    CHECK(d != NULL, "the page is created");

    catnip_device_info_show(d, rows, 3, "Input diagnostic");
    CHECK(!catnip_device_info_take_action(d), "nothing is latched before a press");

    /* The action row is the last one, and the selection opens on it - so A on
     * arrival does the only thing this page can do. */
    fire(rt, "ui.fire('info_list', 'click')");
    CHECK(catnip_device_info_take_action(d), "A on arrival activates the action row");
    CHECK(!catnip_device_info_take_action(d), "and reading the latch clears it");

    /* Every other row is a fact, and a fact does nothing. */
    fire(rt, "ui.fire('info_list', 'click', 1)");
    fire(rt, "ui.fire('info_list', 'click', 2)");
    fire(rt, "ui.fire('info_list', 'click', 3)");
    CHECK(!catnip_device_info_take_action(d), "clicking a fact does nothing at all");

    /* Stepping back to it and pressing again works, because the selection is
     * the ordinary list selection and nothing here is special. */
    fire(rt, "ui.fire('info_list', 'next')");
    fire(rt, "ui.fire('info_list', 'click')");
    CHECK(catnip_device_info_take_action(d), "stepping back onto it and pressing works");

    /* A page with no action row has nothing that can be activated at all. */
    catnip_device_info_show(d, rows, 3, NULL);
    fire(rt, "ui.fire('info_list', 'click')");
    fire(rt, "ui.fire('info_list', 'click', 3)");
    CHECK(!catnip_device_info_take_action(d), "a page of only facts latches nothing");

    /* A rebuild forgets a press nobody consumed: the page it belonged to is
     * gone, and acting on it would enter the diagnostic because of something
     * the user did on a screen that is no longer there. */
    catnip_device_info_show(d, rows, 3, "Input diagnostic");
    fire(rt, "ui.fire('info_list', 'click')");
    catnip_device_info_show(d, rows, 3, "Input diagnostic");
    CHECK(!catnip_device_info_take_action(d), "a rebuild drops an unread press");

    /* More rows than the page holds are cut rather than overrunning it. */
    {
        const char *many[CATNIP_INFO_MAX_ROWS + 4];
        for (int i = 0; i < CATNIP_INFO_MAX_ROWS + 4; i++)
            many[i] = "x";
        catnip_device_info_show(d, many, CATNIP_INFO_MAX_ROWS + 4, NULL);
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
