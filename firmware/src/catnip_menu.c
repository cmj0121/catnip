/* catnip_menu.c - see catnip_menu.h. */
#include "catnip_menu.h"

#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

/* The menu screen, built in Lua so it is the same kind of tree an app builds
 * and the renderer cannot tell the two apart. `__catnip_menu_build` takes the
 * app names and installs the screen; the list owns its selection the same way
 * the File Browser's does, moving it on prev/next, so the input layer that
 * drives an app drives the menu with no special case. Activating a row does not
 * launch here - it hands the one-based index to __catnip_menu_pick, a C bridge
 * that only latches it, because launching from inside this handler would tear
 * down the very tree the handler is running on. */
static const char CATNIP_MENU_LUA[] =
    "function __catnip_menu_build(names)\n"
    "  local status = ui.label{ id = 'menu_status', style = 'caption' }\n"
    "  local rows = {}\n"
    "  for i, name in ipairs(names) do\n"
    "    rows[i] = ui.label{ id = 'menu_app' .. i, text = name }\n"
    "  end\n"
    "  local list\n"
    "  local sel = (#names > 0) and 1 or 0\n"
    "  list = ui.list{ id = 'menu_list',\n"
    "    on_prev = function() if sel > 1 then sel = sel - 1; list.selected = sel end "
    "end,\n"
    "    on_next = function() if sel < #names then sel = sel + 1; list.selected = sel "
    "end end,\n"
    "    on_click = function() if sel >= 1 then __catnip_menu_pick(sel) end end }\n"
    "  list:set_children(rows)\n"
    "  list.selected = sel\n"
    "  __catnip_menu_status = status\n"
    "  ui.screen{ status, list }\n"
    "end\n"
    "\n"
    "function __catnip_menu_set_status(text)\n"
    "  if __catnip_menu_status then __catnip_menu_status.text = text end\n"
    "end\n";

/* Mirrors CATNIP_SHELL_MAX_APPS in catnip_shell.h, which is where the app list
 * this menu is fed from is capped. Kept local rather than pulling the whole
 * shell header in for one number; if the shell's cap moves, move this too. */
#define MENU_MAX_APPS 32

struct catnip_menu {
    catnip_rt *rt;
    char ids[MENU_MAX_APPS][64];
    int n;
    int pick; /* zero-based index of a latched pick, or -1 for none */
};

/* The bridge the menu's on_click calls. It only latches the one-based index the
 * row carries; the map from index to id, and the launch itself, are C's. */
static int menu_pick_cb(lua_State *L)
{
    catnip_menu *m = (catnip_menu *)lua_touserdata(L, lua_upvalueindex(1));
    int idx = (int)luaL_checkinteger(L, 1);

    if (m && idx >= 1 && idx <= m->n) m->pick = idx - 1;
    return 0;
}

catnip_menu *catnip_menu_new(catnip_rt *rt)
{
    lua_State *L;
    catnip_menu *m;

    if (!rt) return NULL;
    L = catnip_rt_lua(rt);
    if (!L) return NULL;

    m = (catnip_menu *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->rt = rt;
    m->pick = -1;

    if (luaL_dostring(L, CATNIP_MENU_LUA) != LUA_OK) {
        catnip_rt_report_error(rt, L);
        free(m);
        return NULL;
    }

    /* The bridge carries the menu pointer as an upvalue rather than reaching a
     * file static, so a second menu on a second runtime would not collide. */
    lua_pushlightuserdata(L, m);
    lua_pushcclosure(L, menu_pick_cb, 1);
    lua_setglobal(L, "__catnip_menu_pick");
    return m;
}

void catnip_menu_show(catnip_menu *m, const catnip_app_entry *apps, int n)
{
    lua_State *L;
    int i;

    if (!m) return;
    L = catnip_rt_lua(m->rt);
    if (!L) return;

    if (n < 0) n = 0;
    if (n > MENU_MAX_APPS) n = MENU_MAX_APPS;
    m->n = n;
    m->pick = -1;
    for (i = 0; i < n; i++)
        snprintf(m->ids[i], sizeof(m->ids[i]), "%s", apps[i].id);

    lua_getglobal(L, "__catnip_menu_build");
    lua_newtable(L);
    for (i = 0; i < n; i++) {
        lua_pushstring(L, apps[i].name);
        lua_rawseti(L, -2, i + 1);
    }
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) catnip_rt_report_error(m->rt, L);
}

const char *catnip_menu_take_pick(catnip_menu *m)
{
    int i;

    if (!m || m->pick < 0) return NULL;
    i = m->pick;
    m->pick = -1;
    return m->ids[i];
}

void catnip_menu_set_status(catnip_menu *m, const char *text)
{
    lua_State *L;

    if (!m) return;
    L = catnip_rt_lua(m->rt);
    if (!L) return;

    lua_getglobal(L, "__catnip_menu_set_status");
    lua_pushstring(L, text ? text : "");
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) catnip_rt_report_error(m->rt, L);
}

void catnip_menu_free(catnip_menu *m)
{
    free(m);
}
