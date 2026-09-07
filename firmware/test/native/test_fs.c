/* Native test for issue #24: fs directory listing (fs.list, fs.stat). */
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

#include "catnip_api.h"
#include "catnip_runtime.h"
#include "catnip_ui.h"
#include "lua.h"

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

static int g_reset_calls;
static int m_sd_reset(void *ud)
{
    (void)ud;
    g_reset_calls++;
    return 0;
}

static const char *SCRIPT =
    "local ok, err = pcall(function()\n"
    "  local items = fs.list()\n"
    "  assert(type(items) == 'table', 'list returns a table')\n"
    "  local byname = {}\n"
    "  for _, it in ipairs(items) do byname[it.name] = it end\n"
    "  assert(byname['a.txt'], 'lists a.txt')\n"
    "  assert(byname['sub'] and byname['sub'].is_dir == true, 'sub is a dir')\n"
    "  assert(byname['a.txt'].is_dir == false, 'a.txt is a file')\n"
    "  assert(byname['a.txt'].size == 5, 'a.txt size is 5')\n"
    "  local sub = fs.list('sub')\n"
    "  assert(#sub == 1 and sub[1].name == 'inner.txt', 'lists subdir contents')\n"
    "  local st = fs.stat('a.txt')\n"
    "  assert(st and st.size == 5 and st.is_dir == false, 'stat a file')\n"
    "  assert(fs.stat('nope') == nil, 'stat missing is nil')\n"
    "  assert(pcall(function() fs.list('..') end) == false, 'list guards ..')\n"
    /* The other half of the contract in device/fs_path.h (#45): a file that is
     * not there is a nil, and only a bad path raises. An app has to be able to
     * tell those apart, and from "there is no card" below. */
    "  assert(fs.read('nope') == nil, 'reading a missing file is nil, not an error')\n"
    "  assert(fs.exists('nope') == false, 'a missing file simply does not exist')\n"
    "  assert(pcall(function() fs.read('../etc/passwd') end) == false,\n"
    "         'read refuses a path that climbs out')\n"
    "  assert(pcall(function() fs.delete('/etc/passwd') end) == false,\n"
    "         'delete refuses an absolute path')\n"
    "  assert(fs.delete('a.txt') == true, 'delete removes a file')\n"
    "  assert(fs.exists('a.txt') == false, 'file is gone after delete')\n"
    "  assert(fs.reset() == true, 'reset reports success')\n"
    "end)\n"
    "RESULT = ok and 'ok' or ('FAIL: ' .. tostring(err))\n";

/* Run against a HAL with no fs_base, which is what there being no card looks
 * like from Lua. */
static const char *NOCARD_SCRIPT =
    "local ok, err = pcall(function() return fs.read('a.txt') end)\n"
    "NOCARD = ok and 'FAIL: no error' or tostring(err)\n";

int main(void)
{
    char base[] = "/tmp/catnip_fslist_XXXXXX";
    if (!mkdtemp(base)) {
        printf("FAIL - mkdtemp\n");
        return 1;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/a.txt", base);
    write_file(path, "hello");
    snprintf(path, sizeof(path), "%s/sub", base);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), "%s/sub/inner.txt", base);
    write_file(path, "x");

    catnip_hal hal;
    memset(&hal, 0, sizeof(hal));
    hal.fs_base = base;
    hal.sd_reset = m_sd_reset;

    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_ui_open(rt);
    catnip_api_open(rt, &hal);

    int rc = catnip_rt_dostring(rt, SCRIPT, "=fs");
    CHECK(rc == 0, "fs script runs");
    lua_State *L = catnip_rt_lua(rt);
    lua_getglobal(L, "RESULT");
    const char *result = lua_tostring(L, -1);
    CHECK(result && strcmp(result, "ok") == 0, result ? result : "(no result)");
    lua_pop(L, 1);
    CHECK(g_reset_calls == 1, "fs.reset reached the HAL");

    catnip_rt_free(rt);

    /* A second runtime with no storage at all, which is what device/hal_meowkit
     * builds when there is no card in the slot: fs_base stays NULL. Every fs.*
     * call must raise rather than answer, so that an app can tell "there is no
     * card" from "that file is not on the card" - the first script above proves
     * the other side of that, where a missing file is a quiet nil. */
    {
        catnip_hal nocard;
        memset(&nocard, 0, sizeof(nocard));

        catnip_rt *rt2 = catnip_rt_new_tracked();
        catnip_ui_open(rt2);
        catnip_api_open(rt2, &nocard);

        int rc2 = catnip_rt_dostring(rt2, NOCARD_SCRIPT, "=nocard");
        CHECK(rc2 == 0, "no-card script runs");
        lua_State *L2 = catnip_rt_lua(rt2);
        lua_getglobal(L2, "NOCARD");
        const char *nc = lua_tostring(L2, -1);
        CHECK(nc && strstr(nc, "fs not available") != NULL,
              "with no card fs.read raises an error rather than returning nil");
        lua_pop(L2, 1);
        catnip_rt_free(rt2);
    }

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
