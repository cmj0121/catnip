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
#include <stdbool.h>

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
    /* What the loader sets for an app it could read and whose API it can
     * satisfy. Without it these fixtures are apps the launcher would rightly
     * refuse, which is not what any of these cases is about. */
    e.compatible = 1;
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
    catnip_menu_show(m, apps, 3, true, NULL);
    CHECK(catnip_menu_take_pick(m) == NULL, "nothing is picked before a click");

    /* The carousel opens on home, which is the mascot and not an app. */
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click')", "=t");
    CHECK(catnip_menu_take_pick(m) == NULL, "activating home launches nothing");

    /* One step right is the first app, two is the second. */
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'next')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'next')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click')", "=t");
    const char *pick = catnip_menu_take_pick(m);
    CHECK(pick != NULL && strcmp(pick, "files") == 0,
          "activating the shown app latches that app's id");
    CHECK(catnip_menu_take_pick(m) == NULL, "and reading the latch clears it");

    /* A carousel is a ring: stepping back from home arrives at the last app,
     * not at a stop.
     *
     * catnip_menu_home() first, because a rebuild now opens where the last
     * launch was made from - that is what makes short B return you to the app
     * you just left - and these cases are about starting from the cat. */
    catnip_menu_home(m);
    catnip_menu_show(m, apps, 3, true, NULL);
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'prev')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click')", "=t");
    pick = catnip_menu_take_pick(m);
    CHECK(pick != NULL && strcmp(pick, "paint") == 0,
          "stepping back from home comes round to the last app");

    /* And forward off the end arrives back at home, which launches nothing. */
    catnip_menu_home(m);
    catnip_menu_show(m, apps, 3, true, NULL);
    for (int i = 0; i < 4; i++)
        catnip_rt_dostring(rt, "ui.fire('menu_list', 'next')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click')", "=t");
    CHECK(catnip_menu_take_pick(m) == NULL, "and forward past the last comes home");

    /* A rebuilt menu picks from the new list, not the old one. */
    catnip_app_entry two[2] = {app("a", "Alpha"), app("b", "Beta")};
    catnip_menu_home(m);
    catnip_menu_show(m, two, 2, true, NULL);
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'next')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'next')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click')", "=t");
    pick = catnip_menu_take_pick(m);
    CHECK(pick != NULL && strcmp(pick, "b") == 0,
          "a rebuilt menu picks from the new apps");

    /* An app that asked for the filesystem cannot run without a card, so the
     * launcher refuses it rather than starting it to fault straight back here.
     * fs.* raises on a device with no storage - see catnip_api.c's fs_path -
     * so launching would only look like the device ignoring the press. */
    catnip_app_entry needs[1] = {app("files", "File Browser")};
    needs[0].needs_fs = 1;
    catnip_menu_home(m);
    catnip_menu_show(m, needs, 1, false, NULL);
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'next')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click')", "=t");
    CHECK(catnip_menu_take_pick(m) == NULL,
          "an app that needs a card will not launch without one");

    catnip_menu_home(m);
    catnip_menu_show(m, needs, 1, true, NULL);
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'next')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click')", "=t");
    pick = catnip_menu_take_pick(m);
    CHECK(pick != NULL && strcmp(pick, "files") == 0, "and launches with one");

    /* No apps at all still has home, and home launches nothing. */
    catnip_menu_home(m);
    catnip_menu_show(m, NULL, 0, true, NULL);
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'next')", "=t");
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click')", "=t");
    CHECK(catnip_menu_take_pick(m) == NULL, "an empty carousel latches no pick");

    /* The status line takes text without a screen having to know its shape. */
    catnip_menu_show(m, apps, 3, true, NULL);
    /* The battery moved into the frame's bar, which is the platform's and not
     * the menu's - so there is no status line here to set any more. */

    /* Leaving an app comes back to the app you left, not to the cat: the spec
     * promises that short B returns you to where you came from, and the thing
     * you had just been looking at was the one exception. Found by id, so an
     * app that is no longer installed quietly means home. */
    catnip_menu_home(m);
    catnip_menu_show(m, apps, 3, true, NULL);
    catnip_rt_dostring(rt, "ui.fire('menu_list', 'click', 3)", "=t");
    CHECK(catnip_menu_take_pick(m) != NULL, "a pick is latched");
    catnip_menu_show(m, apps, 3, true, NULL);
    CHECK(strcmp(catnip_menu_focus_name(m), "File Browser") == 0,
          "and the rebuild opens on the app that was launched");

    catnip_app_entry gone[1] = {app("clock", "Clock")};
    catnip_menu_show(m, gone, 1, true, NULL);
    CHECK(strcmp(catnip_menu_focus_name(m), "") == 0,
          "an app that is no longer there means home");

    catnip_menu_free(m);
    catnip_rt_free(rt);
    if (failures) {
        printf("== %d failure(s) ==\n", failures);
        return 1;
    }
    printf("== ok ==\n");
    return 0;
}
