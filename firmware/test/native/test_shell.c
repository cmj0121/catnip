/* Native test for issue #5: the shell. Discovers apps, launches them, drives
 * their lifecycle back to the menu, and refuses an incompatible app. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "catnip_render.h"
#include "catnip_runtime.h"
#include "catnip_shell.h"
#include "catnip_ui.h"
#include "lua.h"

static unsigned long g_now;
static unsigned long now_fn(void *ud)
{
    (void)ud;
    return g_now;
}
static void pump_fn(void *ud)
{
    (void)ud;
}

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

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}
static void make_app(const char *root, const char *sub, const char *manifest,
                     const char *main_lua)
{
    char dir[256], path[320];
    snprintf(dir, sizeof(dir), "%s/%s", root, sub);
    mkdir(dir, 0777);
    snprintf(path, sizeof(path), "%s/manifest.json", dir);
    write_file(path, manifest);
    snprintf(path, sizeof(path), "%s/main.lua", dir);
    write_file(path, main_lua);
}

/* Drive the shell until it returns to the menu (or a guard trips). */
static void run_to_menu(catnip_shell *sh)
{
    int guard = 0;
    while (catnip_shell_state(sh) == CATNIP_SHELL_RUNNING && ++guard < 1000) {
        catnip_shell_step(sh);
        g_now += 250; /* time advances while the app waits */
    }
}

/* A backend that draws nothing but answers the one call that can fail. The
 * back contract needs real handles - the visible screen is what `back` is
 * posted to - and handles only exist once a pass has run. */
static int be_create(void *ud, catnip_handle h, catnip_handle parent, int index,
                     const catnip_node_desc *d)
{
    (void)ud;
    (void)h;
    (void)parent;
    (void)index;
    (void)d;
    return 0;
}
static const catnip_render_backend g_be = {NULL, NULL, NULL, be_create,
                                           NULL, NULL, NULL, NULL};

/* One press of B, exactly as the device makes it: the input layer posts `back`
 * to the visible screen, the drain runs the app's on_back, and only then does
 * the shell read what it answered. Getting that order wrong is the whole risk,
 * so the test reproduces it rather than calling the shell directly. */
static int press_back(catnip_rt *rt, catnip_shell *sh)
{
    catnip_handle screen = catnip_render_visible_screen(rt);
    if (screen != CATNIP_HANDLE_NONE)
        catnip_render_post_claimable(rt, screen, "back", CATNIP_INDEX_NONE);
    catnip_render_drain(rt);
    return catnip_shell_back(sh);
}

/* Let an app reach residency and put its screen on the (nonexistent) glass. */
static void settle(catnip_rt *rt, catnip_shell *sh)
{
    for (int i = 0; i < 5; i++) {
        catnip_shell_step(sh);
        g_now += 250;
    }
    catnip_render(rt, &g_be);
}

static int global_bool(catnip_rt *rt, const char *name)
{
    lua_State *L = catnip_rt_lua(rt);
    lua_getglobal(L, name);
    int v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

int main(void)
{
    char root[] = "/tmp/catnip_shell_XXXXXX";
    if (!mkdtemp(root)) {
        printf("FAIL - mkdtemp\n");
        return 1;
    }

    make_app(root, "quick",
             "{\"id\":\"quick\",\"name\":\"Quick\",\"catnip_api\":\"1.0\"}",
             "RAN_QUICK = true\n");
    make_app(root, "looper",
             "{\"id\":\"looper\",\"name\":\"Looper\",\"catnip_api\":\"1.0\"}",
             "N = 0\nfor i = 1, 3 do N = N + 1 sys.sleep(500) end\n");
    make_app(root, "resident",
             "{\"id\":\"resident\",\"name\":\"Resident\",\"catnip_api\":\"1.0\"}",
             /* An event-driven app: it builds a screen and lets its main chunk
              * return, meaning to live on through its handlers. */
             "ui.screen{ ui.label{ text = 'hi' } }\n");
    make_app(root, "climber",
             "{\"id\":\"climber\",\"name\":\"Climber\",\"catnip_api\":\"1.0\"}",
             /* The File Browser's shape: B climbs while there is somewhere to
              * climb, and declines at the root so the platform lets it go. */
             "LEVEL = 2\n"
             "ui.screen{ id = 'c', ui.label{ text = 'x' },\n"
             "  on_back = function()\n"
             "    if LEVEL > 0 then LEVEL = LEVEL - 1 return true end\n"
             "    return false\n"
             "  end }\n");
    make_app(root, "deep", "{\"id\":\"deep\",\"name\":\"Deep\",\"catnip_api\":\"1.0\"}",
             /* A confirmation over a root, and no on_back anywhere: the pop is
              * the platform's to do. */
             "ui.screen{ id = 'base', ui.label{ text = 'b' } }\n"
             "ui.push{ id = 'over', ui.label{ text = 'o' } }\n");
    make_app(root, "future",
             "{\"id\":\"future\",\"name\":\"Future\",\"catnip_api\":\"9.0\"}",
             "RAN_FUTURE = true\n");

    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_ui_open(rt); /* an event-driven app builds a screen; ui must be up */
    catnip_shell *sh = catnip_shell_new(rt, root, now_fn, pump_fn, NULL);
    catnip_shell_set_backend(sh, &g_be);
    CHECK(sh != NULL, "shell created");
    CHECK(catnip_shell_count(sh) == 6, "shell lists all apps");
    CHECK(catnip_shell_state(sh) == CATNIP_SHELL_MENU, "starts in the menu");

    char err[128];

    /* Launch a quick app: runs once and returns us to the menu. */
    int rc = catnip_shell_launch_id(sh, "quick", err, sizeof(err));
    CHECK(rc == 0, "quick app launches");
    CHECK(catnip_shell_state(sh) == CATNIP_SHELL_RUNNING, "shell is running the app");
    run_to_menu(sh);
    CHECK(catnip_shell_state(sh) == CATNIP_SHELL_MENU, "returns to menu when app ends");
    CHECK(global_bool(rt, "RAN_QUICK"), "quick app actually ran");

    /* Launch a looping app: cooperatively sleeps, then finishes. */
    rc = catnip_shell_launch_id(sh, "looper", err, sizeof(err));
    CHECK(rc == 0, "looper app launches");
    run_to_menu(sh);
    CHECK(catnip_shell_state(sh) == CATNIP_SHELL_MENU, "looper returns to menu");
    {
        lua_State *L = catnip_rt_lua(rt);
        lua_getglobal(L, "N");
        int n = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        CHECK(n == 3, "looper completed all iterations");
    }

    /* An event-driven app stays resident: its main chunk returns after building
     * a screen, which is not the same as finishing. This is the case host tests
     * missed until it reached a device - the File Browser flashed up and fell
     * straight back to the menu because a clean return was read as "done". */
    rc = catnip_shell_launch_id(sh, "resident", err, sizeof(err));
    CHECK(rc == 0, "resident app launches");
    for (int i = 0; i < 5; i++) {
        catnip_shell_step(sh);
        g_now += 250;
    }
    CHECK(catnip_shell_state(sh) == CATNIP_SHELL_RUNNING,
          "an app that built a screen and returned stays resident, not back to the menu");
    CHECK(catnip_ui_has_screen(rt), "and its screen is still up");
    catnip_shell_exit(sh);
    CHECK(catnip_shell_state(sh) == CATNIP_SHELL_MENU,
          "B backs the resident app out to the menu");

    /* --- short B: the app decides, the platform decides when it does not --- */

    printf("short B climbs the app's levels, then leaves\n");
    rc = catnip_shell_launch_id(sh, "climber", err, sizeof(err));
    CHECK(rc == 0, "climber launches");
    settle(rt, sh);
    CHECK(press_back(rt, sh) == CATNIP_SHELL_RUNNING,
          "a claimed back keeps the app - it climbed a level of its own");
    {
        lua_State *L = catnip_rt_lua(rt);
        lua_getglobal(L, "LEVEL");
        CHECK(lua_tointeger(L, -1) == 1, "and the app's own level actually moved");
        lua_pop(L, 1);
    }
    CHECK(press_back(rt, sh) == CATNIP_SHELL_RUNNING, "it can climb again");
    CHECK(press_back(rt, sh) == CATNIP_SHELL_MENU,
          "and when it declines at its root, the platform lets it go");

    printf("short B pops a pushed screen before it leaves anything\n");
    rc = catnip_shell_launch_id(sh, "deep", err, sizeof(err));
    CHECK(rc == 0, "deep launches");
    settle(rt, sh);
    CHECK(catnip_ui_depth(rt) == 2, "it is showing a screen over its root");
    CHECK(press_back(rt, sh) == CATNIP_SHELL_RUNNING,
          "B with no on_back anywhere does not throw the app away");
    CHECK(catnip_ui_depth(rt) == 1, "it closed the pushed screen instead");
    CHECK(press_back(rt, sh) == CATNIP_SHELL_MENU, "and the next B leaves at the root");

    printf("long B goes home from any depth, and asks nobody\n");
    rc = catnip_shell_launch_id(sh, "climber", err, sizeof(err));
    CHECK(rc == 0, "climber launches again");
    settle(rt, sh);
    {
        /* The climber would claim a back here - it has levels left. Home is not
         * a back, so it is never asked, and the level it would have climbed
         * stays exactly where it was. */
        lua_State *L = catnip_rt_lua(rt);
        lua_getglobal(L, "LEVEL");
        int before = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        CHECK(catnip_shell_home(sh) == CATNIP_SHELL_MENU,
              "home leaves an app that would have claimed the back");
        lua_getglobal(L, "LEVEL");
        CHECK((int)lua_tointeger(L, -1) == before, "and no on_back ran on the way out");
        lua_pop(L, 1);
    }
    CHECK(!catnip_ui_has_screen(rt), "the screen stack is unwound");

    /* An incompatible app is refused and we stay in the menu. */
    rc = catnip_shell_launch_id(sh, "future", err, sizeof(err));
    CHECK(rc != 0, "incompatible app is refused");
    CHECK(catnip_shell_state(sh) == CATNIP_SHELL_MENU, "stays in menu after refusal");
    CHECK(!global_bool(rt, "RAN_FUTURE"), "incompatible app did not run");

    /* Exit mid-run returns to the menu. */
    catnip_shell_launch_id(sh, "looper", err, sizeof(err));
    catnip_shell_step(sh); /* start it */
    catnip_shell_exit(sh);
    CHECK(catnip_shell_state(sh) == CATNIP_SHELL_MENU, "exit returns to the menu");

    catnip_shell_free(sh);
    catnip_rt_free(rt);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
