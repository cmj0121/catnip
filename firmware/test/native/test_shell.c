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

#include "catnip_runtime.h"
#include "catnip_shell.h"
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
    make_app(root, "future",
             "{\"id\":\"future\",\"name\":\"Future\",\"catnip_api\":\"9.0\"}",
             "RAN_FUTURE = true\n");

    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_shell *sh = catnip_shell_new(rt, root, now_fn, pump_fn, NULL);
    CHECK(sh != NULL, "shell created");
    CHECK(catnip_shell_count(sh) == 3, "shell lists all apps");
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
