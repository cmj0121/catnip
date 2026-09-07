/* catnip_render.c - see catnip_render.h. */
#include "catnip_render.h"

#include <string.h>

#include "lua.h"

/* Push ui.root() onto the stack. Returns 1 if a screen table is on top, else 0
 * (and leaves the stack balanced). */
static int push_root(lua_State *L)
{
    lua_getglobal(L, "ui");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    lua_getfield(L, -1, "root"); /* ui.root function */
    lua_remove(L, -2);           /* drop ui */
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    return 1;
}

/* Raw string field `key` of the table at `idx` (or NULL if it is not a string).
 *
 * The value is left on the stack on purpose, and that slot is the only thing
 * keeping the returned pointer alive: nothing else in the tree need own the
 * string, because lua_tostring converts a number in place and the result exists
 * nowhere but here. The caller therefore reads the string first and unwinds
 * afterwards, with the lua_settop in catnip_render's loop - never a lua_pop in
 * this helper. Anything pushed here also counts against the stack the caller is
 * standing on; see the loop for why the depth stays trivial. */
static const char *push_raw_str(lua_State *L, int idx, const char *key)
{
    idx = lua_absindex(L, idx); /* the push below would shift a relative index */
    lua_pushstring(L, key);
    lua_rawget(L, idx);
    return lua_tostring(L, -1);
}

/* A node's displayed text: node.props.text (props is a raw field). Pushes what
 * it read and leaves it there, on the same terms as push_raw_str. */
static const char *node_text(lua_State *L, int node)
{
    lua_pushstring(L, "props");
    lua_rawget(L, lua_absindex(L, node));
    if (!lua_istable(L, -1)) return "";
    const char *t = push_raw_str(L, -1, "text");
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
        /* One stack frame per node. Everything the node's strings point into
         * sits inside that frame, so the backend call happens while the frame
         * still stands and the settop below is what ends their lifetime - which
         * is exactly what catnip_render.h promises a backend. The frame is five
         * slots deep at most (node, kind, id, props, text) on top of the two
         * this function holds, well inside the LUA_MINSTACK Lua guarantees. */
        for (int i = 1; i <= n; i++) {
            int frame = lua_gettop(L);
            lua_rawgeti(L, -1, i); /* child node */
            if (lua_istable(L, -1)) {
                int node = lua_gettop(L);
                const char *kind = push_raw_str(L, node, "kind");
                const char *id = push_raw_str(L, node, "id");
                const char *text = node_text(L, node);
                if (kind && strcmp(kind, "button") == 0) {
                    if (be->button) be->button(be->ud, id ? id : "", text);
                } else {
                    if (be->label) be->label(be->ud, id ? id : "", text);
                }
                count++;
            }
            lua_settop(L, frame); /* child, and the strings read out of it */
        }
    }
    lua_pop(L, 1); /* children */

    if (be->end_screen) be->end_screen(be->ud);

    lua_pop(L, 1); /* screen */
    return count;
}
