/* Native test for issue #4: manifest parsing + the app loader. Builds a small
 * apps tree in a temp dir, then discovers, opens, and runs apps -- including the
 * API-incompatibility and bad-manifest paths. */
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

#include "catnip_loader.h"
#include "catnip_manifest.h"
#include "catnip_runtime.h"
#include "lua.h"

static int failures;
#define CHECK(cond, name)                                                    \
    do {                                                                     \
        if (cond) { printf("  ok   - %s\n", name); }                        \
        else { printf("  FAIL - %s\n", name); failures++; }                 \
    } while (0)

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(content, f); fclose(f); }
}

static void make_app(const char *root, const char *sub, const char *manifest,
                     const char *main_lua)
{
    char dir[256], path[320];
    snprintf(dir, sizeof(dir), "%s/%s", root, sub);
    mkdir(dir, 0777);
    snprintf(path, sizeof(path), "%s/manifest.json", dir);
    write_file(path, manifest);
    if (main_lua) {
        snprintf(path, sizeof(path), "%s/main.lua", dir);
        write_file(path, main_lua);
    }
}

static const catnip_app_entry *find(const catnip_app_entry *e, int n, const char *id)
{
    for (int i = 0; i < n; i++)
        if (strcmp(e[i].id, id) == 0) return &e[i];
    return NULL;
}

int main(void)
{
    /* --- manifest parsing units --- */
    catnip_manifest m;
    char err[128];
    CHECK(catnip_manifest_parse(
              "{\"id\":\"a\",\"name\":\"A\",\"catnip_api\":\"1.0\"}", &m, err,
              sizeof(err)) == 0,
          "valid manifest parses");
    CHECK(strcmp(m.entry, "main.lua") == 0, "entry defaults to main.lua");
    CHECK(catnip_manifest_parse("{\"name\":\"A\",\"catnip_api\":\"1.0\"}", &m,
                                err, sizeof(err)) != 0,
          "missing id is rejected");
    CHECK(catnip_manifest_parse("{\"id\":\"a\",\"name\":\"A\"}", &m, err,
                                sizeof(err)) != 0,
          "missing catnip_api is rejected");

    /* --- filesystem discovery + open --- */
    char root[] = "/tmp/catnip_apps_XXXXXX";
    if (!mkdtemp(root)) { printf("FAIL - mkdtemp\n"); return 1; }

    make_app(root, "hello",
             "{\"id\":\"hello\",\"name\":\"Hello\",\"catnip_api\":\"1.0\","
             "\"icon\":\"icon.png\"}",
             "LOADED = 'hello ran'\n");
    make_app(root, "future",
             "{\"id\":\"future\",\"name\":\"Future\",\"catnip_api\":\"2.0\"}",
             "LOADED = 'should not run'\n");
    make_app(root, "bad", "{ not valid json", "x = 1\n");

    catnip_app_entry apps[8];
    int n = catnip_loader_discover(root, apps, 8);
    CHECK(n == 3, "discovery finds all app folders");

    const catnip_app_entry *hello = find(apps, n, "hello");
    const catnip_app_entry *future = find(apps, n, "future");
    CHECK(hello && hello->compatible == 1, "compatible app is marked compatible");
    CHECK(hello && strcmp(hello->icon, "icon.png") == 0, "manifest icon is carried into the entry");
    CHECK(future && future->compatible == 0, "future-API app is marked incompatible");

    /* open + run the compatible app */
    char hdir[256];
    snprintf(hdir, sizeof(hdir), "%s/hello", root);
    char *code = NULL;
    int rc = catnip_loader_open(hdir, &m, &code, err, sizeof(err));
    CHECK(rc == 0 && code != NULL, "compatible app opens");
    if (rc == 0) {
        CHECK(strcmp(m.id, "hello") == 0, "manifest id read");
        catnip_rt *rt = catnip_rt_new_tracked();
        catnip_rt_dostring(rt, code, "=hello");
        lua_State *L = catnip_rt_lua(rt);
        lua_getglobal(L, "LOADED");
        const char *loaded = lua_tostring(L, -1);
        CHECK(loaded && strcmp(loaded, "hello ran") == 0, "entry script runs");
        lua_pop(L, 1);
        catnip_rt_free(rt);
        free(code);
    }

    /* incompatible app is refused with a helpful message */
    char fdir[256];
    snprintf(fdir, sizeof(fdir), "%s/future", root);
    code = NULL;
    rc = catnip_loader_open(fdir, &m, &code, err, sizeof(err));
    CHECK(rc != 0 && code == NULL, "incompatible app is refused");
    CHECK(strstr(err, "catnip_api") != NULL, "refusal explains the API mismatch");

    /* bad manifest is refused */
    char bdir[256];
    snprintf(bdir, sizeof(bdir), "%s/bad", root);
    rc = catnip_loader_open(bdir, &m, &code, err, sizeof(err));
    CHECK(rc != 0, "bad manifest is refused");

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
