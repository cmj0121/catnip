/* catnip_app_grid.c - see catnip_app_grid.h. */
#include "catnip_app_grid.h"

#include <stdlib.h>
#include <string.h>

#include "catnip_pins.h"
#include "lauxlib.h"
#include "lua.h"

#define GRID_MAX_APPS 32

/* A grid of app cells the ring walks. The one thing here that is not an
 * ordinary list is the touch guard: on_click carries the tapped index for a
 * finger and none for A, so a tap only moves the selection and A is what
 * launches. Pinning is on_options, which only long-A produces - a finger never
 * reaches it. */
static const char CATNIP_GRID_LUA[] =
    "function __catnip_grid_build(names, ids, ready, pinned, open)\n"
    "  local cells = {}\n"
    "  for i, name in ipairs(names) do\n"
    /* An unpinned app is drawn in the quieter role, which on a picture cell the
     * platform renders as a fainter icon - the marker a user reads to see what
     * a pin did. Not `disabled`: an unpinned app is perfectly launchable, and
     * saying otherwise with the greying reserved for "cannot run" would be the
     * grid lying about two different things in the same ink. */
    "    cells[i] = ui.label{ id = 'grid' .. i, text = name, image = ids[i],\n"
    "                         icon = 'placeholder', disabled = not ready[i],\n"
    "                         style = pinned[i] and 'body' or 'caption' }\n"
    "  end\n"
    /* Nothing is selected on arrival - `sel` 0, which the renderer reads as no
     * selection at all and draws no ring for.
     *
     * A grid opened with its first cell already ringed is a grid that has made
     * a choice on the user's behalf before they have looked at it, and on a
     * screen that is mostly pictures the ring is the loudest thing on it. The
     * first direction press is what starts choosing; until then this is a page
     * being read, not a menu being operated.
     *
     * A grid clamps at its ends rather than wrapping: it is a directory with a
     * first and a last, not a ring. */
    "  local sel = open\n"
    "  local list\n"
    "  list = ui.list{ id = 'grid_list', layout = 'grid',\n"
    "    on_prev = function()\n"
    "      if sel == 0 then sel = 1\n"
    "      elseif sel > 1 then sel = sel - 1 end\n"
    "      list.selected = sel\n"
    "    end,\n"
    "    on_next = function()\n"
    "      if sel == 0 then sel = 1\n"
    "      elseif sel < #cells then sel = sel + 1 end\n"
    "      list.selected = sel\n"
    "    end,\n"
    "    on_click = function(self, i)\n"
    /* A tap carries the cell it landed on: it selects, and stops there. A is
     * the press with no index, and it launches. That is the whole touch guard -
     * a finger cannot enter, only point.
     *
     * A with nothing selected does nothing, rather than launching whatever
     * happens to be first: there is no selection to act on, and inventing one
     * at the moment of the press is how a device starts the wrong app. */
    "      if i then sel = i; list.selected = sel\n"
    "      elseif sel >= 1 and ready[sel] then __catnip_grid_pick(sel) end\n"
    "    end,\n"
    /* Long-A pins or unpins the selected app - and, like A, needs one. Never
     * reached by touch. */
    "    on_options = function(self, i)\n"
    "      if sel >= 1 then __catnip_grid_pin(sel) end\n"
    "    end }\n"
    "  list:set_children(cells)\n"
    "  list.selected = sel\n"
    "  ui.screen{ list }\n"
    "end\n";

struct catnip_app_grid {
    catnip_rt *rt;
    char ids[GRID_MAX_APPS][64];
    bool ready[GRID_MAX_APPS];
    int n;
    int pick; /* zero-based, or -1 */
    int pin;  /* zero-based, or -1 */
};

static int grid_pick_cb(lua_State *L)
{
    catnip_app_grid *g = (catnip_app_grid *)lua_touserdata(L, lua_upvalueindex(1));
    int idx = (int)luaL_checkinteger(L, 1);
    if (g && idx >= 1 && idx <= g->n) g->pick = idx - 1;
    return 0;
}

static int grid_pin_cb(lua_State *L)
{
    catnip_app_grid *g = (catnip_app_grid *)lua_touserdata(L, lua_upvalueindex(1));
    int idx = (int)luaL_checkinteger(L, 1);
    if (g && idx >= 1 && idx <= g->n) g->pin = idx - 1;
    return 0;
}

catnip_app_grid *catnip_app_grid_new(catnip_rt *rt)
{
    lua_State *L;
    catnip_app_grid *g;

    if (!rt) return NULL;
    L = catnip_rt_lua(rt);
    if (!L) return NULL;
    g = (catnip_app_grid *)calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->rt = rt;
    g->pick = -1;
    g->pin = -1;
    if (luaL_dostring(L, CATNIP_GRID_LUA) != LUA_OK) {
        catnip_rt_report_error(rt, L);
        free(g);
        return NULL;
    }
    lua_pushlightuserdata(L, g);
    lua_pushcclosure(L, grid_pick_cb, 1);
    lua_setglobal(L, "__catnip_grid_pick");
    lua_pushlightuserdata(L, g);
    lua_pushcclosure(L, grid_pin_cb, 1);
    lua_setglobal(L, "__catnip_grid_pin");
    return g;
}

void catnip_app_grid_free(catnip_app_grid *g)
{
    free(g);
}

void catnip_app_grid_show(catnip_app_grid *g, const catnip_app_entry *apps, int n,
                          bool fs_ready, const char *unpinned)
{
    lua_State *L;
    int i;

    if (!g) return;
    L = catnip_rt_lua(g->rt);
    if (!L) return;
    if (n < 0) n = 0;
    if (n > GRID_MAX_APPS) n = GRID_MAX_APPS;
    g->n = n;
    g->pick = -1;
    g->pin = -1;

    for (i = 0; i < n; i++) {
        snprintf(g->ids[i], sizeof(g->ids[i]), "%s", apps[i].id);
        g->ready[i] = apps[i].compatible && (fs_ready || !apps[i].needs_fs);
    }

    lua_getglobal(L, "__catnip_grid_build");
    lua_newtable(L); /* names */
    for (i = 0; i < n; i++) {
        lua_pushstring(L, apps[i].name);
        lua_rawseti(L, -2, i + 1);
    }
    lua_newtable(L); /* ids, for the icon */
    for (i = 0; i < n; i++) {
        lua_pushstring(L, apps[i].id);
        lua_rawseti(L, -2, i + 1);
    }
    lua_newtable(L); /* ready */
    for (i = 0; i < n; i++) {
        lua_pushboolean(L, g->ready[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_newtable(L); /* pinned: not in the unpinned set */
    for (i = 0; i < n; i++) {
        lua_pushboolean(L, !catnip_pins_contains(unpinned ? unpinned : "", apps[i].id));
        lua_rawseti(L, -2, i + 1);
    }
    /* 0: no selection. The first direction press picks the first cell up. */
    lua_pushinteger(L, 0);
    if (lua_pcall(L, 5, 0, 0) != LUA_OK) catnip_rt_report_error(g->rt, L);
}

const char *catnip_app_grid_take_pick(catnip_app_grid *g)
{
    int i;
    if (!g || g->pick < 0) return NULL;
    i = g->pick;
    g->pick = -1;
    return g->ids[i];
}

const char *catnip_app_grid_take_pin(catnip_app_grid *g)
{
    int i;
    if (!g || g->pin < 0) return NULL;
    i = g->pin;
    g->pin = -1;
    return g->ids[i];
}
