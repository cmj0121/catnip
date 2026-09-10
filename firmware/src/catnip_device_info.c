/* catnip_device_info.c - see catnip_device_info.h. */
#include "catnip_device_info.h"

#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

/* Rows of text with a pair of buttons under them: the facts, and the two doors.
 *
 * Three focus stops, in the order they are read: the facts, which up and down
 * scroll, then the two buttons, which A activates. */
static const char CATNIP_INFO_LUA[] =
    "function __catnip_info_build(lines, left, right)\n"
    "  local rows = {}\n"
    "  for i, line in ipairs(lines) do\n"
    "    rows[i] = ui.label{ id = 'info' .. i, text = line }\n"
    "  end\n"
    /* The facts are a column that scrolls and nothing more: up and down move
     * the cursor, and A on a fact does nothing, because a page where A on
     * "free heap" lit something up would promise what it cannot deliver.
     *
     * It clamps rather than wrapping. A ring is the home carousel's shape,
     * where there is no top and no bottom to see; a column has both, and one
     * that jumped from the last line back to the first would lose the reader
     * their place in a page they are reading rather than choosing from. */
    "  local sel = 1\n"
    "  local list\n"
    "  list = ui.list{ id = 'info_list',\n"
    "    on_prev = function()\n"
    "      if sel > 1 then sel = sel - 1; list.selected = sel end\n"
    "    end,\n"
    "    on_next = function()\n"
    "      if sel < #rows then sel = sel + 1; list.selected = sel end\n"
    "    end }\n"
    "  list:set_children(rows)\n"
    "  list.selected = sel\n"
    /* The two places you would go having read them, side by side under the
     * facts, each with the icon it is known by elsewhere - the same gear the
     * preference page is reached by everywhere else, so the button is
     * recognised before it is read. They are ordinary buttons: left and right
     * move between them and A activates, which is the platform's own rule and
     * needs nothing new here. */
    "  local keys = {}\n"
    "  local function key(k, id, act)\n"
    "    if not k then return end\n"
    "    keys[#keys + 1] = ui.button{ id = id, text = k.text, icon = k.icon,\n"
    "                                 on_click = function() __catnip_info_act(act) end "
    "}\n"
    "  end\n"
    "  key(left, 'info_left', 1)\n"
    "  key(right, 'info_right', 2)\n"
    "  local bar = ui.list{ id = 'info_keys', layout = 'row' }\n"
    "  bar:set_children(keys)\n"
    "  ui.screen{ list, bar, id = 'info_screen' }\n"
    "end\n"
    /* Rewrite the facts without rebuilding the page.
     *
     * They go stale while they are being read - a card comes out, the battery
     * moves, the heap moves - and rebuilding the screen to say so would throw
     * away the ring's position and the list's scroll, which is precisely what a
     * retained renderer exists to keep. One property write per line that
     * actually differs, so a page of unchanged facts costs no repaint at all -
     * which matters here, because this display repaints whole. */
    "function __catnip_info_update(lines)\n"
    "  for i, line in ipairs(lines) do\n"
    "    local node = ui.get('info' .. i)\n"
    "    if node and node.text ~= line then node.text = line end\n"
    "  end\n"
    "end\n";

struct catnip_device_info {
    catnip_rt *rt;
    int action;
    int rows; /* how many fact lines are on the page, for update() */
};

static int info_action_cb(lua_State *L)
{
    catnip_device_info *d = (catnip_device_info *)lua_touserdata(L, lua_upvalueindex(1));
    if (d) d->action = (int)luaL_checkinteger(L, 1);
    return 0;
}

catnip_device_info *catnip_device_info_new(catnip_rt *rt)
{
    lua_State *L;
    catnip_device_info *d;

    if (!rt) return NULL;
    L = catnip_rt_lua(rt);
    if (!L) return NULL;

    d = (catnip_device_info *)calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->rt = rt;

    if (luaL_dostring(L, CATNIP_INFO_LUA) != LUA_OK) {
        catnip_rt_report_error(rt, L);
        free(d);
        return NULL;
    }
    lua_pushlightuserdata(L, d);
    lua_pushcclosure(L, info_action_cb, 1);
    lua_setglobal(L, "__catnip_info_act");
    return d;
}

/* A button, or nil for a page that has fewer than two. A table rather than two
 * more arguments: the label and the icon are one thing said twice over, and a
 * call site with four bare strings in a row is one transposition away from a
 * gear on the diagnostic. */
static void push_key(lua_State *L, const catnip_info_key *k)
{
    if (!k || !k->text) {
        lua_pushnil(L);
        return;
    }
    lua_newtable(L);
    lua_pushstring(L, k->text);
    lua_setfield(L, -2, "text");
    lua_pushstring(L, k->icon ? k->icon : "none");
    lua_setfield(L, -2, "icon");
}

void catnip_device_info_show(catnip_device_info *d, const char *const *rows, int n,
                             const catnip_info_key *left, const catnip_info_key *right)
{
    lua_State *L;
    int i;

    if (!d) return;
    L = catnip_rt_lua(d->rt);
    if (!L) return;

    if (n < 0) n = 0;
    if (n > CATNIP_INFO_MAX_ROWS) n = CATNIP_INFO_MAX_ROWS;
    d->rows = n;
    d->action = CATNIP_INFO_NONE;

    lua_getglobal(L, "__catnip_info_build");
    lua_newtable(L);
    for (i = 0; i < n; i++) {
        lua_pushstring(L, rows[i] ? rows[i] : "");
        lua_rawseti(L, -2, i + 1);
    }
    push_key(L, left);
    push_key(L, right);
    if (lua_pcall(L, 3, 0, 0) != LUA_OK) catnip_rt_report_error(d->rt, L);
}

void catnip_device_info_update(catnip_device_info *d, const char *const *rows, int n)
{
    lua_State *L;
    int i;

    if (!d) return;
    L = catnip_rt_lua(d->rt);
    if (!L) return;
    if (n < 0) n = 0;
    if (n > CATNIP_INFO_MAX_ROWS) n = CATNIP_INFO_MAX_ROWS;
    /* A different number of lines is a different page: writing the first few
     * over the old ones would leave the tail of the previous page underneath
     * them. The caller shows() in that case; this only ever rewrites. */
    if (n != d->rows) return;

    lua_getglobal(L, "__catnip_info_update");
    lua_newtable(L);
    for (i = 0; i < n; i++) {
        lua_pushstring(L, rows[i] ? rows[i] : "");
        lua_rawseti(L, -2, i + 1);
    }
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) catnip_rt_report_error(d->rt, L);
}

int catnip_device_info_take_action(catnip_device_info *d)
{
    int a;

    if (!d) return CATNIP_INFO_NONE;
    a = d->action;
    d->action = CATNIP_INFO_NONE;
    return a;
}

void catnip_device_info_free(catnip_device_info *d)
{
    free(d);
}
