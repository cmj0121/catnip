/* catnip_ui.c - see catnip_ui.h. The ui framework is written in Lua (below) and
 * loaded into the runtime; the widget tree is plain Lua tables so the renderer
 * and tests can both walk it. */
#include "catnip_ui.h"

#include "lauxlib.h"
#include "lua.h"

/* The ui module. Nodes are tables { kind, props, children, handlers, id };
 * a metatable makes node.text read/write props so apps can say status.text=...
 *
 * Those names are raw fields, and that is the one thing to hold on to about the
 * metatable: Lua consults __index and __newindex only for a key the table does
 * not already have, so neither ever fires for kind, props, children, handlers
 * or dirty. Reading one gives the field and not the prop, and writing one
 * replaces the field without reaching props and without marking anything. The
 * renderer reads them with rawget for that reason, so the two sides agree. What
 * an app gets from it: `node.children` is the children, `node.text` is a prop,
 * and a prop that collides with a field name is unreachable through the node.
 *
 * `id` is the trap in that list, because it is a raw field only on a node that
 * was named: `id = props.id` leaves the key absent when the app named nothing.
 * So `node.id = 'x'` writes the field on a named node - past props, past
 * `dirty` - and writes props.id on an unnamed one, where the renderer, reading
 * raw, will still report no name. Neither updates state.by_id, so ui.get keeps
 * answering to the old name and not to the new one. Rename nothing after it is
 * built; `id` is the name a node was given, and the renderer treats it as a
 * label for logs rather than a key precisely because of this.
 *
 * Structure is the one thing `dirty` does not cover, and does not need to.
 * catnip_render.c walks `children` on every pass whatever `dirty` says and
 * matches each child by table identity, so the traversal is what decides that a
 * row was added, moved or dropped; `dirty` only says that this node's own props
 * are worth re-reading. node:set_children() marks anyway, so a caller that
 * replaces a list's rows does not leave the two disagreeing - but a mutation
 * made in place through `node.children` still marks nothing, and is still seen,
 * because the traversal is the authority and not the flag.
 *
 * The visible screen is the top of a stack rather than a single root, because
 * a confirmation that replaced the whole screen would rebuild the screen
 * underneath it on dismissal - losing the focus and the scroll position that
 * the retained renderer exists to keep. A pushed screen is a screen and not a
 * modal: there is no dimmed backdrop and no partial cover, because those are
 * geometry and geometry is not exposed. */
static const char CATNIP_UI_LUA[] =
    "local ui = {}\n"
    "local state = { by_id = {}, stack = {} }\n"
    "local MAX_DEPTH = 4\n"
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
    "-- ui.get reaches every screen on the stack, so the index is rebuilt from\n"
    "-- the whole stack whenever the stack changes. Doing it any other way would\n"
    "-- leave a popped screen's ids answering lookups.\n"
    "local function reindex()\n"
    "  state.by_id = {}\n"
    "  for _, s in ipairs(state.stack) do index_tree(s) end\n"
    "end\n"
    "\n"
    "-- Children are read as node.children. There is no node:children(): a\n"
    "-- method of that name could never be called, because `children` is a raw\n"
    "-- field, so __index never fires and the lookup hands back the table.\n"
    "-- Replacing them is a method for the mirror-image reason - `node.children\n"
    "-- = rows` cannot fire __newindex either, so this is that assignment with\n"
    "-- the bookkeeping still attached: the node is marked, and the id index is\n"
    "-- rebuilt so a row that arrived with a name is one ui.get can find.\n"
    "function methods.set_children(t, kids)\n"
    "  rawset(t, 'children', kids)\n"
    "  rawset(t, 'dirty', true)\n"
    "  reindex()\n"
    "end\n"
    "\n"
    "function ui.label(spec)  return make('label', spec)  end\n"
    "function ui.button(spec) return make('button', spec) end\n"
    "function ui.list(spec)   return make('list', spec)   end\n"
    "\n"
    "-- ui.screen is 'this app's screen', not a navigation verb: it replaces the\n"
    "-- whole stack. That is the one moment where discarding focus and scroll is\n"
    "-- correct, because the screen the user was looking at no longer exists.\n"
    "function ui.screen(spec)\n"
    "  local node = make('screen', spec)\n"
    "  state.stack = { node }\n"
    "  reindex()\n"
    "  return node\n"
    "end\n"
    "\n"
    "-- Show a screen above the current one, keeping that one intact - its\n"
    "-- widgets, and so its focus and its scroll. Takes a spec, exactly as\n"
    "-- ui.screen does, or a screen node that was built earlier; returns the\n"
    "-- screen it pushed, or nil if it refused.\n"
    "--\n"
    "-- Telling a spec from a node is not politeness. ui.screen is the only way\n"
    "-- to build a screen node, and it replaces the stack as it goes, so\n"
    "-- ui.push(ui.screen{...}) would push what it had just installed. Handing\n"
    "-- an already-built node to make() instead would quietly fold its kind,\n"
    "-- props and children into the props of a new one.\n"
    "--\n"
    "-- The cap is what stops a handler in a loop from growing the stack until\n"
    "-- the heap gives out; catnip_render.c holds the same number and the two\n"
    "-- have to move together.\n"
    "function ui.push(spec)\n"
    "  if type(spec) ~= 'table' then return nil end\n"
    "  if #state.stack >= MAX_DEPTH then\n"
    "    print('catnip: ui.push refused, the screen stack is already '\n"
    "          .. MAX_DEPTH .. ' deep')\n"
    "    return nil\n"
    "  end\n"
    "  local node = rawget(spec, 'kind') and spec or make('screen', spec)\n"
    "  state.stack[#state.stack + 1] = node\n"
    "  reindex()\n"
    "  return node\n"
    "end\n"
    "\n"
    "-- Discard the top screen and show the one beneath. Returns what it popped,\n"
    "-- or nil if there was nothing to pop.\n"
    "function ui.pop()\n"
    "  if #state.stack == 0 then return nil end\n"
    "  local top = table.remove(state.stack)\n"
    "  reindex()\n"
    "  return top\n"
    "end\n"
    "\n"
    "-- Let go of every screen. This is lifetime rather than app sugar: the Lua\n"
    "-- heap is accounted, so a tree still rooted here after an app ends is not\n"
    "-- merely garbage, it is charged to whatever runs next. The renderer calls\n"
    "-- it as the last step of its own teardown.\n"
    "function ui.reset()\n"
    "  state.stack = {}\n"
    "  state.by_id = {}\n"
    "end\n"
    "\n"
    "function ui.get(id)  return state.by_id[id] end\n"
    "-- No argument: the visible screen. With one: the n-th screen from the\n"
    "-- bottom, which is how the renderer reads the stack whole and can tell a\n"
    "-- push from a replacement without either side declaring which it was.\n"
    "function ui.root(n)\n"
    "  if n ~= nil then return state.stack[n] end\n"
    "  return state.stack[#state.stack]\n"
    "end\n"
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
