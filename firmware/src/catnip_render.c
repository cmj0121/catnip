/* catnip_render.c - see catnip_render.h. */
#include "catnip_render.h"

#include <string.h>

#include "lua.h"

/* Push ui.root() onto the stack. Returns 1 if a screen table is on top, else 0
 * (and leaves the stack balanced). */
static int push_root(lua_State *L)
{
    lua_getglobal(L, "ui");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 0; }
    lua_getfield(L, -1, "root"); /* ui.root function */
    lua_remove(L, -2);           /* drop ui */
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) { lua_pop(L, 1); return 0; }
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 0; }
    return 1;
}

/* Raw string field of the table at the top of the stack (or NULL). */
static const char *raw_str(lua_State *L, const char *key)
{
    lua_pushstring(L, key);
    lua_rawget(L, -2);
    const char *s = lua_tostring(L, -1);
    lua_pop(L, 1);
    return s;
}

/* Read a node's displayed text: node.props.text (props is a raw field). */
static const char *node_text(lua_State *L)
{
    lua_pushstring(L, "props");
    lua_rawget(L, -2);
    const char *t = NULL;
    if (lua_istable(L, -1)) t = raw_str(L, "text");
    lua_pop(L, 1);
    return t ? t : "";
}

int catnip_render(catnip_rt *rt, const catnip_render_backend *be)
{
    lua_State *L = catnip_rt_lua(rt);
    if (!L || !be) return -1;
    if (!push_root(L)) return -1; /* screen table now on top */

    if (be->begin_screen) be->begin_screen(be->ud);

    int count = 0;
    lua_pushstring(L, "children");
    lua_rawget(L, -2); /* children array */
    if (lua_istable(L, -1)) {
        int n = (int)lua_rawlen(L, -1);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i); /* child node */
            if (lua_istable(L, -1)) {
                const char *kind = raw_str(L, "kind");
                const char *id = raw_str(L, "id");
                const char *text = node_text(L);
                if (kind && strcmp(kind, "button") == 0) {
                    if (be->button) be->button(be->ud, id ? id : "", text);
                } else {
                    if (be->label) be->label(be->ud, id ? id : "", text);
                }
                count++;
            }
            lua_pop(L, 1); /* child */
        }
    }
    lua_pop(L, 1); /* children */

    if (be->end_screen) be->end_screen(be->ud);

    lua_pop(L, 1); /* screen */
    return count;
}
