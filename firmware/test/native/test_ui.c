/* Native test for issue #2: the ui.* framework. Builds a widget tree, looks up
 * nodes by id, mutates a property, and dispatches an event to a callback -- the
 * whole retained-builder + callback contract, with no LVGL/device needed. */
#include <stdio.h>
#include <string.h>

#include "catnip_runtime.h"
#include "catnip_ui.h"
#include "lauxlib.h"
#include "lua.h"

static int failures;
#define CHECK(cond, name)                                                    \
    do {                                                                     \
        if (cond) { printf("  ok   - %s\n", name); }                        \
        else { printf("  FAIL - %s\n", name); failures++; }                 \
    } while (0)

static const char *SCRIPT =
    "local ok, err = pcall(function()\n"
    "  local s = ui.screen{\n"
    "    ui.label{ id = 'title', text = 'hi' },\n"
    "    ui.button{ id = 'go', text = 'Go', on_click = function() FIRED = true end },\n"
    "  }\n"
    "  assert(s.kind == 'screen', 'screen kind')\n"
    "  assert(#s.children == 2, 'child count')\n"
    "  assert(ui.get('title').text == 'hi', 'label text via id')\n"
    "  ui.get('title').text = 'bye'\n"
    "  assert(ui.get('title').text == 'bye', 'property is mutable')\n"
    "  FIRED = false\n"
    "  assert(ui.fire('go', 'click') == true, 'fire dispatches')\n"
    "  assert(FIRED == true, 'callback ran')\n"
    "  assert(ui.fire('nope', 'click') == false, 'unknown id is a no-op')\n"
    "  assert(ui.root() == s, 'root is the last screen')\n"
    "end)\n"
    "RESULT = ok and 'ok' or ('FAIL: ' .. tostring(err))\n";

static const char *global_str(catnip_rt *rt, const char *name)
{
    lua_State *L = catnip_rt_lua(rt);
    lua_getglobal(L, name);
    const char *s = lua_tostring(L, -1);
    lua_pop(L, 1);
    return s ? s : "(nil)";
}

int main(void)
{
    catnip_rt *rt = catnip_rt_new_tracked();
    CHECK(catnip_ui_open(rt) == 0, "ui module loads");

    /* ui is reachable as a global and via the catnip namespace. */
    catnip_rt_dostring(rt, "UI_T = type(ui); CUI_T = type(catnip.ui)", "=t");
    CHECK(strcmp(global_str(rt, "UI_T"), "table") == 0, "ui is a global table");
    CHECK(strcmp(global_str(rt, "CUI_T"), "table") == 0, "catnip.ui is bound");

    int rc = catnip_rt_dostring(rt, SCRIPT, "=ui");
    CHECK(rc == 0, "ui script runs");
    const char *result = global_str(rt, "RESULT");
    CHECK(strcmp(result, "ok") == 0, result);

    catnip_rt_free(rt);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
