/* Native test for issues #26/#27: the File Browser demo app. Drives the real
 * apps/filebrowser/main.lua against ui.* and a temp SD tree, exercising
 * navigation, open (read), delete (with confirm) and reset. */
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

#ifndef APP_MAIN
#define APP_MAIN "../apps/filebrowser/main.lua"
#endif

static int g_reset_calls;
static int m_sd_reset(void *ud) { (void)ud; g_reset_calls++; return 0; }

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

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *b = malloc(n + 1);
    fread(b, 1, n, f);
    fclose(f);
    b[n] = 0;
    return b;
}

/* The driver exercises the app through its `browser` table, exactly as the
 * device input layer would, and records the outcome in RESULT. */
static const char *DRIVER =
    "local function sel(name)\n"
    "  for i, e in ipairs(browser.entries) do\n"
    "    if e.name == name then browser.sel = i return end\n"
    "  end\n"
    "  error('not found: ' .. name)\n"
    "end\n"
    "local ok, err = pcall(function()\n"
    "  assert(#browser.entries == 3, 'lists 3 entries at root')\n"
    "  assert(ui.get('title').text == 'SD:/', 'title shows root')\n"
    "  -- navigate into the docs folder\n"
    "  sel('docs'); browser.enter()\n"
    "  assert(browser.cwd == 'docs', 'entered docs')\n"
    "  assert(ui.get('title').text == 'SD:/docs', 'title updates')\n"
    "  assert(#browser.entries == 1, 'docs has one file')\n"
    "  browser.up()\n"
    "  assert(browser.cwd == '', 'up returns to root')\n"
    "  -- open a file for reading\n"
    "  sel('a.txt'); browser.open()\n"
    "  assert(browser.mode == 'view', 'open enters view mode')\n"
    "  assert(browser.view == 'hello', 'viewer shows file contents')\n"
    "  assert(ui.get('viewer').text == 'hello', 'viewer widget shows contents')\n"
    "  browser.back()\n"
    "  assert(browser.mode == 'list', 'back returns to the list')\n"
    "  -- delete a file, with confirmation\n"
    "  sel('b.txt'); browser.delete()\n"
    "  assert(browser.mode == 'confirm', 'delete asks to confirm')\n"
    "  browser.confirm()\n"
    "  assert(fs.exists('b.txt') == false, 'file deleted after confirm')\n"
    "  -- cancel path leaves things intact\n"
    "  sel('a.txt'); browser.delete(); browser.cancel()\n"
    "  assert(fs.exists('a.txt') == true, 'cancel keeps the file')\n"
    "  -- reset the SD card, with confirmation\n"
    "  browser.reset_sd(); browser.confirm()\n"
    "end)\n"
    "RESULT = ok and 'ok' or ('FAIL: ' .. tostring(err))\n";

int main(void)
{
    char base[] = "/tmp/catnip_fb_XXXXXX";
    if (!mkdtemp(base)) { printf("FAIL - mkdtemp\n"); return 1; }
    char p[512];
    snprintf(p, sizeof(p), "%s/a.txt", base); write_file(p, "hello");
    snprintf(p, sizeof(p), "%s/b.txt", base); write_file(p, "bye");
    snprintf(p, sizeof(p), "%s/docs", base); mkdir(p, 0777);
    snprintf(p, sizeof(p), "%s/docs/note.txt", base); write_file(p, "note");

    char *app = read_file(APP_MAIN);
    if (!app) { printf("FAIL - cannot read %s\n", APP_MAIN); return 1; }

    catnip_hal hal;
    memset(&hal, 0, sizeof(hal));
    hal.fs_base = base;
    hal.sd_reset = m_sd_reset;

    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_ui_open(rt);
    catnip_api_open(rt, &hal);

    int rc = catnip_rt_dostring(rt, app, "=filebrowser");
    CHECK(rc == 0, "file browser app loads");

    rc = catnip_rt_dostring(rt, DRIVER, "=driver");
    CHECK(rc == 0, "driver runs");
    lua_State *L = catnip_rt_lua(rt);
    lua_getglobal(L, "RESULT");
    const char *result = lua_tostring(L, -1);
    CHECK(result && strcmp(result, "ok") == 0, result ? result : "(no result)");
    lua_pop(L, 1);
    CHECK(g_reset_calls == 1, "reset SD reached the HAL");

    free(app);
    catnip_rt_free(rt);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
