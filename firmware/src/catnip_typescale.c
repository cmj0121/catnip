/* catnip_typescale.c - see catnip_typescale.h. */
#include "catnip_typescale.h"

#include <stdlib.h>

#include "lauxlib.h"
#include "lua.h"

/* Each line is drawn in the role it names, largest first, which is the whole
 * design: a page that described the sizes in one size would be describing them
 * rather than showing them.
 *
 * `display` takes digits and nothing else - it exists for a quantity that has
 * earned the whole panel - so its line is a time, which is the quantity it was
 * cut for.
 *
 * `layout = 'text'` for the same reason the device page uses it: nothing here
 * is selectable, so nothing should be drawn as though it were. Up and down move
 * the reading position and the view follows it.
 */
static const char CATNIP_TYPESCALE_LUA[] =
    "function __catnip_typescale_build()\n"
    "  local rows = {\n"
    "    ui.label{ id = 'ts_display', text = '12:34', style = 'display' },\n"
    "    ui.label{ id = 'ts_title',   text = 'Title 20 Aa Bb', style = 'title' },\n"
    "    ui.label{ id = 'ts_body',    text = 'Body 16 Aa Bb Cc Dd', style = 'body' },\n"
    "    ui.label{ id = 'ts_caption', text = 'Caption 10 Aa Bb Cc Dd Ee',\n"
    "              style = 'caption' },\n"
    "  }\n"
    "  local sel = 1\n"
    "  local list\n"
    "  list = ui.list{ id = 'ts_list', layout = 'text',\n"
    "    on_prev = function()\n"
    "      if sel > 1 then sel = sel - 1; list.selected = sel end\n"
    "    end,\n"
    "    on_next = function()\n"
    "      if sel < #rows then sel = sel + 1; list.selected = sel end\n"
    "    end }\n"
    "  list:set_children(rows)\n"
    "  list.selected = sel\n"
    /* Nothing claims B: there is nothing here to climb out of, so the platform
     * takes it and the page's owner puts the device page back. */
    "  ui.screen{ list, id = 'ts_screen' }\n"
    "end\n";

struct catnip_typescale {
    catnip_rt *rt;
};

catnip_typescale *catnip_typescale_new(catnip_rt *rt)
{
    lua_State *L;
    catnip_typescale *t;

    if (!rt) return NULL;
    L = catnip_rt_lua(rt);
    if (!L) return NULL;

    t = (catnip_typescale *)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->rt = rt;

    if (luaL_dostring(L, CATNIP_TYPESCALE_LUA) != LUA_OK) {
        catnip_rt_report_error(rt, L);
        free(t);
        return NULL;
    }
    return t;
}

void catnip_typescale_show(catnip_typescale *t)
{
    lua_State *L;

    if (!t) return;
    L = catnip_rt_lua(t->rt);
    if (!L) return;
    lua_getglobal(L, "__catnip_typescale_build");
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) catnip_rt_report_error(t->rt, L);
}

void catnip_typescale_free(catnip_typescale *t)
{
    free(t);
}
