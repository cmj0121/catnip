/*
 * Native test for issue #33: the launcher menu's launch-and-return latch.
 *
 * Drawing the menu is LVGL and needs the device, but the part the shell depends
 * on is plain: the menu lists the installed apps, moving the selection on
 * prev/next the way the input layer will drive it, and activating a row latches
 * which app was picked - by id - for the main loop to launch at a safe point.
 * That latch is what turns a click into a launch without the handler tearing
 * down its own tree, so it is worth pinning here.
 *
 * The handlers are fired through ui.fire, which is the same entry the real
 * dispatch uses; what this cannot exercise is the coroutine the device runs it
 * on, which test_render already covers.
 */
#include <stdio.h>
#include <string.h>

#include "catnip_loader.h"
#include "catnip_menu.h"
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

static catnip_app_entry app(const char *id, const char *name)
{
    catnip_app_entry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.id, sizeof(e.id), "%s", id);
    snprintf(e.name, sizeof(e.name), "%s", name);
    return e;
}

int main(void)
{
    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_ui_open(rt);
    catnip_menu *m = catnip_menu_new(rt);

    printf("the launcher menu lists apps and latches a pick\n");
    CHECK(m != NULL, "the menu is created");

    catnip_app_entry apps[3] = {app("clock", "Clock"), app("files", "File Browser"),
                                app("paint", "Paint")};
    catnip_menu_show(m, apps, 3);
    CHECK(catnip_menu_take_pick(m) == NULL, "nothing is picked before a click");

    /* Move the highlight to the second app, then activate it. */
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'next')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click')", "=t");
    const char *pick = catnip_menu_take_pick(m);
    CHECK(pick != NULL && strcmp(pick, "files") == 0,
          "activating the highlighted row latches that app's id");
    CHECK(catnip_menu_take_pick(m) == NULL, "and reading the latch clears it");

    /* prev/next clamp at the ends: two 'prev' from the top stays on the first. */
    catnip_menu_show(m, apps, 3);
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'prev')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'prev')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click')", "=t");
    pick = catnip_menu_take_pick(m);
    CHECK(pick != NULL && strcmp(pick, "clock") == 0,
          "the selection clamps at the top rather than wrapping");

    /* A rebuilt menu picks from the new list, not the old one. */
    catnip_app_entry two[2] = {app("a", "Alpha"), app("b", "Beta")};
    catnip_menu_show(m, two, 2);
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'next')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click')", "=t");
    pick = catnip_menu_take_pick(m);
    CHECK(pick != NULL && strcmp(pick, "b") == 0,
          "a rebuilt menu picks from the new apps");

    /* An empty menu has nothing to activate. */
    catnip_menu_show(m, NULL, 0);
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click')", "=t");
    CHECK(catnip_menu_take_pick(m) == NULL, "an empty menu latches no pick");

    /* The status line takes text without a screen having to know its shape. */
    catnip_menu_show(m, apps, 3);
    catnip_menu_set_status(m, "Battery 80%");
    CHECK(1, "set_status does not fault");

    catnip_menu_free(m);
    catnip_rt_free(rt);
    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
