/* catnip_device_info.c - see catnip_device_info.h. */
#include "catnip_device_info.h"

#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"

/* A page to be read: one column of lines, and nothing on it that acts.
 *
 * It was rows of text with two tiles under them, and the tiles were the thing
 * this page was least entitled to have - operations drawn on a screen, on the
 * one page in the device whose whole point is that it answers before it offers.
 * They are behind A now, where every operation is, and what is left is prose.
 */
static const char CATNIP_INFO_LUA[] =
    "function __catnip_info_build(lines)\n"
    "  local rows = {}\n"
    "  for i, line in ipairs(lines) do\n"
    "    rows[i] = ui.label{ id = 'info' .. i, text = line }\n"
    "  end\n"
    /* The cursor is the reading position and is not drawn - `layout = 'text'`
     * is what says so. It clamps rather than wrapping: a ring is the home
     * carousel's shape, where there is no top and no bottom to see; a column
     * has both, and one that jumped from the last line back to the first would
     * lose the reader their place in a page they are reading. */
    "  local sel = 1\n"
    "  local list\n"
    "  list = ui.list{ id = 'info_list', layout = 'text',\n"
    "    on_prev = function()\n"
    "      if sel > 1 then sel = sel - 1; list.selected = sel end\n"
    "    end,\n"
    "    on_next = function()\n"
    "      if sel < #rows then sel = sel + 1; list.selected = sel end\n"
    "    end,\n"
    /* A and long A answer the same three, and answering is all this does: the
     * platform draws them. Short A as well as long, because this page has no
     * selection for long A to be about and nothing else short A could mean -
     * the same degenerate case the clock's face is. */
    "    on_click = function() return { 'pref', 'sizes', 'diag' } end,\n"
    "    on_options = function() return { 'pref', 'sizes', 'diag' } end,\n"
    "    on_action = function(self, id)\n"
    "      if id == 'pref' then __catnip_info_act(1)\n"
    "      elseif id == 'sizes' then __catnip_info_act(2)\n"
    "      elseif id == 'diag' then __catnip_info_act(3) end\n"
    "    end }\n"
    "  list:set_children(rows)\n"
    "  list.selected = sel\n"
    "  ui.screen{ list, id = 'info_screen' }\n"
    "end\n"
    /* Rewrite the facts without rebuilding the page.
     *
     * They go stale while they are being read - a card comes out, the battery
     * moves, the heap moves - and rebuilding the screen to say so would throw
     * away the reading position and the scroll, which is precisely what a
     * retained renderer exists to keep. One property write per line that
     * actually differs, so a page of unchanged facts costs no repaint at all -
     * which matters here, because this display repaints whole. */
    "function __catnip_info_update(lines)\n"
    "  for i, line in ipairs(lines) do\n"
    "    local node = ui.get('info' .. i)\n"
    "    if node and node.text ~= line then node.text = line end\n"
    "  end\n"
    "end\n";

/* The three, with the icons each is known by elsewhere: the gear the preference
 * page is reached by everywhere, a page of letters for the type specimen, and
 * the warning triangle for the one that takes the panel and gives it back only
 * when it is told to. Recognised before they are read - which is what lets the
 * names be five letters. Three cells across 320 px leaves about seventy for
 * words, and a name that does not fit is drawn with an ellipsis; the icon is
 * carrying most of the meaning either way, so the short name is the honest one
 * rather than a truncation of a long one.
 *
 * None is destructive. The diagnostic takes the screen and hands it back; the
 * preference page can be left with B; and a page of type samples changes
 * nothing at all - so B carries the last of them, which is what makes this a
 * bar of three that steps rather than one that binds. */
static const catnip_action kActions[] = {
    {"pref", "Prefs", "settings", false},
    {"sizes", "Sizes", "file", false},
    {"diag", "Diag", "warning", false},
};

const catnip_action *catnip_device_info_actions(int *n)
{
    if (n) *n = (int)(sizeof(kActions) / sizeof(kActions[0]));
    return kActions;
}

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

void catnip_device_info_show(catnip_device_info *d, const char *const *rows, int n)
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
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) catnip_rt_report_error(d->rt, L);
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
