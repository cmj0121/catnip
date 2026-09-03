/*
 * catnip_runtime.c - see catnip_runtime.h.
 *
 * The one interesting choice here is that print() is replaced with a version
 * that writes to the runtime's log sink instead of stdout, so scripts behave
 * the same on a host (tests) and on the device (Serial / on-screen log).
 */
#include "catnip_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

struct catnip_rt {
    lua_State *L;
    catnip_log_fn log;
    void *log_ud;
};

/* Where the print closure finds its runtime: a light-userdata upvalue. */
static catnip_rt *rt_of(lua_State *L)
{
    return (catnip_rt *)lua_touserdata(L, lua_upvalueindex(1));
}

static void emit(catnip_rt *rt, const char *msg, size_t len)
{
    if (rt && rt->log) {
        rt->log(rt->log_ud, msg, len);
    } else {
        fwrite(msg, 1, len, stdout);
        fputc('\n', stdout);
    }
}

/* print(...) -> one log line, fields separated by tabs, like stock Lua. */
static int catnip_print(lua_State *L)
{
    catnip_rt *rt = rt_of(L);
    int n = lua_gettop(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; i++) {
        size_t len;
        const char *s = luaL_tolstring(L, i, &len); /* pushes the string */
        if (i > 1) luaL_addchar(&b, '\t');
        luaL_addlstring(&b, s, len);
        lua_pop(L, 1); /* pop the string luaL_tolstring pushed */
    }
    luaL_pushresult(&b);
    size_t out_len;
    const char *out = lua_tolstring(L, -1, &out_len);
    emit(rt, out, out_len);
    lua_pop(L, 1);
    return 0;
}

static void install_print(catnip_rt *rt)
{
    lua_State *L = rt->L;
    lua_pushlightuserdata(L, rt);
    lua_pushcclosure(L, catnip_print, 1);
    lua_setglobal(L, "print");
}

static catnip_rt *rt_create(lua_State *L)
{
    if (!L) return NULL;
    catnip_rt *rt = (catnip_rt *)calloc(1, sizeof(*rt));
    if (!rt) {
        lua_close(L);
        return NULL;
    }
    rt->L = L;
    luaL_openlibs(L);
    install_print(rt);
    return rt;
}

catnip_rt *catnip_rt_new(void)
{
    return rt_create(luaL_newstate());
}

catnip_rt *catnip_rt_new_alloc(catnip_alloc_fn alloc, void *alloc_ud)
{
    if (!alloc) return catnip_rt_new();
    /* lua_Alloc has the same shape as catnip_alloc_fn. */
    return rt_create(lua_newstate((lua_Alloc)alloc, alloc_ud));
}

void catnip_rt_set_log(catnip_rt *rt, catnip_log_fn fn, void *ud)
{
    if (!rt) return;
    rt->log = fn;
    rt->log_ud = ud;
}

lua_State *catnip_rt_lua(catnip_rt *rt) { return rt ? rt->L : NULL; }

/* A traceback message handler so runtime errors carry a stack, not just text. */
static int msgh(lua_State *L)
{
    const char *msg = lua_tostring(L, 1);
    if (msg == NULL) msg = "(non-string error)";
    luaL_traceback(L, L, msg, 1);
    return 1;
}

static int run_protected(catnip_rt *rt, int load_status, const char *what)
{
    lua_State *L = rt->L;
    if (load_status != LUA_OK) {
        size_t len;
        const char *err = lua_tolstring(L, -1, &len);
        emit(rt, err ? err : "load error", err ? len : 10);
        lua_pop(L, 1);
        return load_status;
    }
    int base = lua_gettop(L);      /* the loaded function */
    lua_pushcfunction(L, msgh);
    lua_insert(L, base);           /* msgh below the function */
    int st = lua_pcall(L, 0, 0, base);
    lua_remove(L, base);           /* remove msgh */
    if (st != LUA_OK) {
        size_t len;
        const char *err = lua_tolstring(L, -1, &len);
        emit(rt, err ? err : "runtime error", err ? len : 13);
        lua_pop(L, 1);
    }
    (void)what;
    return st;
}

int catnip_rt_dostring(catnip_rt *rt, const char *code, const char *chunkname)
{
    if (!rt || !code) return -1;
    const char *name = chunkname ? chunkname : "=(catnip)";
    int ld = luaL_loadbuffer(rt->L, code, strlen(code), name);
    return run_protected(rt, ld, name);
}

int catnip_rt_dofile(catnip_rt *rt, const char *path)
{
    if (!rt || !path) return -1;
    int ld = luaL_loadfile(rt->L, path);
    return run_protected(rt, ld, path);
}

void catnip_rt_free(catnip_rt *rt)
{
    if (!rt) return;
    if (rt->L) lua_close(rt->L);
    free(rt);
}
