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
#define CHECK(cond, name)                                                                \
    do {                                                                                 \
        if (cond) {                                                                      \
            printf("  ok   - %s\n", name);                                               \
        } else {                                                                         \
            printf("  FAIL - %s\n", name);                                               \
            failures++;                                                                  \
        }                                                                                \
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
    "  -- Children are a raw field, so they are read as a field and replaced\n"
    "  -- through the method. The method exists because the plain assignment\n"
    "  -- cannot fire __newindex and would mark nothing.\n"
    "  assert(type(s.children) == 'table', 'children is the field, not a method')\n"
    "  rawset(s, 'dirty', false)\n"
    "  local kept = s.children[1]\n"
    "  s:set_children{ kept, ui.label{ id = 'added', text = 'a' } }\n"
    "  assert(#s.children == 2, 'set_children replaced the children')\n"
    "  assert(s.children[1] == kept, 'and the kept row is the very same node')\n"
    "  assert(rawget(s, 'dirty') == true, 'a structural change is marked')\n"
    "  assert(ui.get('added') ~= nil, 'and the new row is in the id index')\n"
    "  assert(ui.get('go') == nil, 'while the row it replaced is not')\n"
    "  -- The other raw fields, pinned as the known trap rather than left to be\n"
    "  -- found by an app that renames a widget. `id` is a raw field only on a\n"
    "  -- node that was named, so the same line of code writes the field on one\n"
    "  -- node and props.id on another - and neither reaches state.by_id, which\n"
    "  -- is why the renderer treats `id` as a label for logs and not as a key.\n"
    "  local named, anon = ui.label{ id = 'n', text = 't' }, ui.label{ text = 't' }\n"
    "  ui.screen{ named, anon }\n"
    "  rawset(named, 'dirty', false)\n"
    "  rawset(anon, 'dirty', false)\n"
    "  named.id = 'n2'\n"
    "  anon.id = 'a2'\n"
    "  assert(rawget(named, 'id') == 'n2', 'a named node takes the write raw')\n"
    "  assert(rawget(named, 'dirty') == false, 'so nothing marks it')\n"
    "  assert(rawget(anon, 'id') == nil, 'an unnamed one never gains the field')\n"
    "  assert(rawget(anon, 'props').id == 'a2', 'the write lands in props')\n"
    "  assert(ui.get('n') == named, 'the old name still answers')\n"
    "  assert(ui.get('n2') == nil and ui.get('a2') == nil, 'the new one does not')\n"
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
