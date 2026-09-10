/*
 * Native test for the app grid and pinning (#71).
 *
 * The grid launches an app and pins one on or off the ring, and the one thing
 * it must never do is let a finger do either. That is the whole guard: a tap
 * carries the cell it landed on, a joystick A carries none, so a tap selects
 * and only A launches - and pinning is long-A, which touch never produces.
 * This pins that distinction, because it is the difference between a directory
 * you can browse with a thumb and one that starts the wrong app when you brush
 * the glass.
 */
#include <stdio.h>
#include <string.h>

#include "catnip_app_grid.h"
#include "catnip_runtime.h"
#include "catnip_sched.h"
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

static catnip_rt *g_rt;

static void fire(const char *lua)
{
    catnip_rt_dostring(g_rt, lua, "=t");
}

static catnip_app_entry app(const char *id, const char *name)
{
    catnip_app_entry a;
    memset(&a, 0, sizeof(a));
    snprintf(a.id, sizeof(a.id), "%s", id);
    snprintf(a.name, sizeof(a.name), "%s", name);
    a.compatible = 1;
    a.needs_fs = 0;
    return a;
}

int main(void)
{
    catnip_sched *sched;
    catnip_app_grid *g;
    catnip_app_entry apps[3];
    const char *s;

    g_rt = catnip_rt_new_tracked();
    catnip_ui_open(g_rt);
    sched = catnip_sched_new(g_rt, NULL, NULL, NULL);
    catnip_render_set_dispatch(g_rt, catnip_sched_dispatch, sched);
    g = catnip_app_grid_new(g_rt);

    apps[0] = app("dino", "Dino");
    apps[1] = app("clock", "Clock");
    apps[2] = app("files", "Files");

    printf("the app grid launches and pins, and a finger does neither\n");
    CHECK(g != NULL, "the grid is built");

    /* "clock" is unpinned, so it is the one the grid marks as off the ring; the
     * others are pinned. The show does not fail on the set. */
    catnip_app_grid_show(g, apps, 3, true, "clock");
    CHECK(catnip_app_grid_take_pick(g) == NULL, "nothing is picked before a press");
    CHECK(catnip_app_grid_take_pin(g) == NULL, "and nothing is pinned");

    /* A tap carries the cell index: it selects and stops. No pick. */
    fire("ui.fire('grid_list', 'click', 2)");
    CHECK(catnip_app_grid_take_pick(g) == NULL,
          "a tap selects a cell but does not launch it");

    /* A - a click with no index - launches the selected app, which the tap
     * above moved to the second cell. */
    fire("ui.fire('grid_list', 'click')");
    s = catnip_app_grid_take_pick(g);
    CHECK(s && strcmp(s, "clock") == 0, "A launches the selected app");
    CHECK(catnip_app_grid_take_pick(g) == NULL, "and reading the pick clears it");

    /* Long-A - on_options - pins or unpins the selected, and a finger never
     * reaches on_options. */
    fire("ui.fire('grid_list', 'options')");
    s = catnip_app_grid_take_pin(g);
    CHECK(s && strcmp(s, "clock") == 0, "long-A pins the selected app");
    CHECK(catnip_app_grid_take_pin(g) == NULL, "and reading the pin clears it");

    /* Stepping and then A launches the one stepped to. */
    catnip_app_grid_show(g, apps, 3, true, "");
    fire("ui.fire('grid_list', 'next')");
    fire("ui.fire('grid_list', 'click')");
    s = catnip_app_grid_take_pick(g);
    CHECK(s && strcmp(s, "clock") == 0, "next then A launches the second app");

    /* A rebuild drops a press nobody read, so a pick made on a grid that is
     * gone cannot launch an app on the screen that replaced it. */
    fire("ui.fire('grid_list', 'click')");
    catnip_app_grid_show(g, apps, 3, true, "");
    CHECK(catnip_app_grid_take_pick(g) == NULL, "a rebuild drops an unread press");

    catnip_app_grid_free(g);
    catnip_sched_free(sched);
    catnip_rt_free(g_rt);
    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
