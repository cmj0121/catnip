/* catnip_ui.c - see catnip_ui.h. The ui framework is written in Lua (below) and
 * loaded into the runtime; the widget tree is plain Lua tables so the renderer
 * and tests can both walk it. */
#include "catnip_ui.h"

#include "lauxlib.h"
#include "lua.h"

/* The ui module. Nodes are tables { kind, props, children, handlers, id };
 * a metatable makes node.text read/write props so apps can say status.text=... */
static const char CATNIP_UI_LUA[] =
    "local ui = {}\n"
    "local state = { by_id = {}, root = nil }\n"
    "\n"
    "local methods = {}\n"
    "local WidgetMT = {}\n"
    "WidgetMT.__index = function(t, k)\n"
    "  local v = rawget(t, 'props')[k]\n"
    "  if v ~= nil then return v end\n"
    "  return methods[k]\n"
    "end\n"
    "WidgetMT.__newindex = function(t, k, v)\n"
    "  rawget(t, 'props')[k] = v\n"
    "  rawset(t, 'dirty', true)\n"
    "end\n"
    "\n"
    "function methods.children(t) return rawget(t, 'children') end\n"
    "\n"
    "local function make(kind, spec)\n"
    "  spec = spec or {}\n"
    "  local props, children, handlers = {}, {}, {}\n"
    "  for k, v in pairs(spec) do\n"
    "    if type(k) == 'number' then\n"
    "      children[#children + 1] = v\n"
    "    elseif type(k) == 'string' and k:sub(1, 3) == 'on_' then\n"
    "      handlers[k] = v\n"
    "    else\n"
    "      props[k] = v\n"
    "    end\n"
    "  end\n"
    "  return setmetatable({ kind = kind, props = props, children = children,\n"
    "                        handlers = handlers, id = props.id, dirty = true },\n"
    "                      WidgetMT)\n"
    "end\n"
    "\n"
    "local function index_tree(node)\n"
    "  if node.id then state.by_id[node.id] = node end\n"
    "  for _, c in ipairs(rawget(node, 'children')) do index_tree(c) end\n"
    "end\n"
    "\n"
    "function ui.label(spec)  return make('label', spec)  end\n"
    "function ui.button(spec) return make('button', spec) end\n"
    "function ui.list(spec)   return make('list', spec)   end\n"
    "\n"
    "function ui.screen(spec)\n"
    "  local node = make('screen', spec)\n"
    "  state.root = node\n"
    "  state.by_id = {}\n"
    "  index_tree(node)\n"
    "  return node\n"
    "end\n"
    "\n"
    "function ui.get(id)  return state.by_id[id] end\n"
    "function ui.root()   return state.root end\n"
    "\n"
    "function ui.fire(target, event, ...)\n"
    "  local node = target\n"
    "  if type(target) == 'string' then node = state.by_id[target] end\n"
    "  if not node then return false end\n"
    "  local h = rawget(node, 'handlers')\n"
    "  local fn = h[event] or h['on_' .. event]\n"
    "  if not fn then return false end\n"
    "  fn(...)\n"
    "  return true\n"
    "end\n"
    "\n"
    "return ui\n";

int catnip_ui_open(catnip_rt *rt)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L) return -1;

    if (luaL_dostring(L, CATNIP_UI_LUA) != LUA_OK) {
        catnip_rt_report_error(rt, L);
        return -1;
    }
    /* stack: ui table (returned by the chunk) */
    lua_pushvalue(L, -1);
    lua_setglobal(L, "ui"); /* global ui */

    lua_getglobal(L, "catnip");
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, -2);      /* ui */
        lua_setfield(L, -2, "ui"); /* catnip.ui = ui */
    }
    lua_pop(L, 2); /* catnip (or its nil) + ui */
    return 0;
}
