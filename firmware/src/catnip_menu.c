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
    "function __catnip_menu_build(names, ids, ready)\n"
    /* Home is the cat, and the apps sit either side of it: one carousel, one
     * `selected`, stepped left and right. The mascot is the first position
     * rather than a screen of its own, so arriving at it and leaving it cost
     * the same one gesture as anything else on the plane. */
    "  local rows = { ui.label{ id = 'menu_home', text = '', icon = 'mascot' } }\n"
    "  for i, name in ipairs(names) do\n"
    /* `image` names the app, and the platform resolves it to that app's own
     * icon.png. An app that shipped none resolves to nothing and falls back to
     * the placeholder glyph, which is what that glyph exists for. */
    /* `disabled` is the platform's word for "here but not usable"; the backend
     * dims it and the handler below refuses to launch it. */
    "    rows[i + 1] = ui.label{ id = 'menu_app' .. i, text = name,\n"
    "                            image = ids[i], icon = 'placeholder',\n"
    "                            disabled = not ready[i] }\n"
    "  end\n"
    "  local list\n"
    "  local sel = 1\n"
    "  list = ui.list{ id = 'menu_list', layout = 'carousel',\n"
    /* A carousel is a ring, not a column: stepping past either end comes round
     * rather than stopping. A list clamps, and that difference is the shape
     * itself - a column has a top and a bottom you can see, and a ring does
     * not, so stopping dead in one reads as an end and in the other as a
     * device that stopped listening. */
    "    on_prev = function()\n"
    "      sel = sel - 1\n"
    "      if sel < 1 then sel = #rows end\n"
    "      list.selected = sel\n"
    "      __catnip_menu_focus(sel)\n"
    "    end,\n"
    "    on_next = function()\n"
    "      sel = sel + 1\n"
    "      if sel > #rows then sel = 1 end\n"
    "      list.selected = sel\n"
    "      __catnip_menu_focus(sel)\n"
    "    end,\n"
    /* A tap names the position it landed on, exactly as in any other list. */
    "    on_click = function(self, i)\n"
    "      if i then sel = i; list.selected = sel end\n"
    /* Position 1 is home; there is nothing to launch there. */
    "      if sel > 1 and ready[sel - 1] then __catnip_menu_pick(sel - 1) end\n"
    "    end }\n"
    "  list:set_children(rows)\n"
    "  list.selected = sel\n"
    "  ui.screen{ list }\n"
    "end\n";

/* Mirrors CATNIP_SHELL_MAX_APPS in catnip_shell.h, which is where the app list
 * this menu is fed from is capped. Kept local rather than pulling the whole
 * shell header in for one number; if the shell's cap moves, move this too. */
#define MENU_MAX_APPS 32

struct catnip_menu {
    catnip_rt *rt;
    char ids[MENU_MAX_APPS][64];
    char names[MENU_MAX_APPS][64]; /* kept so the header can name the focus */
    int n;
    int pick;  /* zero-based index of a latched pick, or -1 for none */
    int focus; /* one-based carousel position; 1 is home */
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

/* Which position the carousel is showing, one-based, so the frame's header can
 * name it. It is latched rather than asked for, because the answer lives in the
 * Lua closure that moves the selection and nothing in C is in a position to
 * read it. */
static int menu_focus_cb(lua_State *L)
{
    catnip_menu *m = (catnip_menu *)lua_touserdata(L, lua_upvalueindex(1));
    int i = (int)luaL_checkinteger(L, 1);
    if (m) m->focus = i;
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
    m->focus = 1;

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
    lua_pushlightuserdata(L, m);
    lua_pushcclosure(L, menu_focus_cb, 1);
    lua_setglobal(L, "__catnip_menu_focus");
    return m;
}

void catnip_menu_show(catnip_menu *m, const catnip_app_entry *apps, int n, bool fs_ready)
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
    m->focus = 1; /* every rebuild opens on home */
    for (i = 0; i < n; i++) {
        snprintf(m->ids[i], sizeof(m->ids[i]), "%s", apps[i].id);
        snprintf(m->names[i], sizeof(m->names[i]), "%s", apps[i].name);
    }

    lua_getglobal(L, "__catnip_menu_build");
    lua_newtable(L); /* names, for the header */
    for (i = 0; i < n; i++) {
        lua_pushstring(L, apps[i].name);
        lua_rawseti(L, -2, i + 1);
    }
    lua_newtable(L); /* ids, which are what an app's own icon is found by */
    for (i = 0; i < n; i++) {
        lua_pushstring(L, apps[i].id);
        lua_rawseti(L, -2, i + 1);
    }
    lua_newtable(L); /* whether each one can actually run right now */
    for (i = 0; i < n; i++) {
        lua_pushboolean(L, apps[i].compatible && (fs_ready || !apps[i].needs_fs));
        lua_rawseti(L, -2, i + 1);
    }
    if (lua_pcall(L, 3, 0, 0) != LUA_OK) catnip_rt_report_error(m->rt, L);
}

const char *catnip_menu_focus_name(const catnip_menu *m)
{
    int i;

    if (!m || m->focus <= 1) return ""; /* home is the cat, and has no name */
    i = m->focus - 2;                   /* past home, into the app list */
    if (i < 0 || i >= m->n) return "";
    return m->names[i];
}

const char *catnip_menu_take_pick(catnip_menu *m)
{
    int i;

    if (!m || m->pick < 0) return NULL;
    i = m->pick;
    m->pick = -1;
    return m->ids[i];
}

void catnip_menu_free(catnip_menu *m)
{
    free(m);
}
