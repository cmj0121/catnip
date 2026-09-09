/* catnip_device_info.c - see catnip_device_info.h. */
#include "catnip_device_info.h"

#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

/* Rows of text, which is the shape the spec already has, so there is nothing
 * here but a list and a selection.
 *
 * Only the action row answers a click. A page of facts that lit up and reacted
 * when you pressed A on "Free heap" would be promising something it cannot do,
 * and the fastest way to teach that A does nothing here is for A to do nothing
 * here. */
static const char CATNIP_INFO_LUA[] =
    "function __catnip_info_build(lines, action)\n"
    "  local rows = {}\n"
    "  for i, line in ipairs(lines) do\n"
    "    rows[i] = ui.label{ id = 'info' .. i, text = line, style = 'caption' }\n"
    "  end\n"
    "  local act = nil\n"
    "  if action then\n"
    "    act = #rows + 1\n"
    /* `primary` and an icon, because this row is the one thing on the page that
     * happens rather than is - and it is the last row, where a reader arrives
     * having already been told everything else. */
    "    rows[act] = ui.label{ id = 'info_action', text = action,\n"
    "                          icon = 'settings', style = 'primary' }\n"
    "  end\n"
    "  local list\n"
    /* The action row is where the selection starts when there is one, so A on
     * arrival does the only thing this page can do. With no action row nothing
     * is selected, because nothing here is selectable. */
    "  local sel = act or 0\n"
    "  list = ui.list{ id = 'info_list',\n"
    "    on_prev = function()\n"
    "      if sel > 1 then sel = sel - 1; list.selected = sel end\n"
    "    end,\n"
    "    on_next = function()\n"
    "      if sel >= 1 and sel < #rows then sel = sel + 1; list.selected = sel end\n"
    "    end,\n"
    "    on_click = function(self, i)\n"
    "      if i then sel = i; list.selected = sel end\n"
    "      if act and sel == act then __catnip_info_action() end\n"
    "    end }\n"
    "  list:set_children(rows)\n"
    "  list.selected = sel\n"
    "  ui.screen{ list, id = 'info_screen' }\n"
    "end\n";

struct catnip_device_info {
    catnip_rt *rt;
    bool action;
};

static int info_action_cb(lua_State *L)
{
    catnip_device_info *d = (catnip_device_info *)lua_touserdata(L, lua_upvalueindex(1));
    if (d) d->action = true;
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
    lua_setglobal(L, "__catnip_info_action");
    return d;
}

void catnip_device_info_show(catnip_device_info *d, const char *const *rows, int n,
                             const char *action)
{
    lua_State *L;
    int i;

    if (!d) return;
    L = catnip_rt_lua(d->rt);
    if (!L) return;

    if (n < 0) n = 0;
    if (n > CATNIP_INFO_MAX_ROWS) n = CATNIP_INFO_MAX_ROWS;
    d->action = false;

    lua_getglobal(L, "__catnip_info_build");
    lua_newtable(L);
    for (i = 0; i < n; i++) {
        lua_pushstring(L, rows[i] ? rows[i] : "");
        lua_rawseti(L, -2, i + 1);
    }
    if (action) lua_pushstring(L, action);
    else lua_pushnil(L);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) catnip_rt_report_error(d->rt, L);
}

bool catnip_device_info_take_action(catnip_device_info *d)
{
    bool a;

    if (!d) return false;
    a = d->action;
    d->action = false;
    return a;
}

void catnip_device_info_free(catnip_device_info *d)
{
    free(d);
}
