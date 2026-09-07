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

#include "catnip_alloc.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

struct catnip_rt {
    lua_State *L;
    catnip_log_fn log;
    void *log_ud;
    catnip_alloc acct; /* used only when `tracked` */
    int tracked;
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
        /* The separator goes in before the conversion, because every luaL_Buffer
         * call except luaL_addvalue addresses the buffer through the top of the
         * stack, and luaL_tolstring leaves its result sitting there. Operate the
         * buffer with that result on top and its first move to the heap removes
         * the converted string instead of the buffer's own slot, which both
         * unanchors the string across the allocation that follows and leaves the
         * heap block where the next pop will free it. luaL_addvalue is the one
         * call that expects the value above the buffer, and it pops it itself. */
        if (i > 1) luaL_addchar(&b, '\t');
        luaL_tolstring(L, i, NULL);
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    /* luaL_pushresult always leaves a string, so lua_tolstring reads it rather
     * than converting a value in place, and the slot holding it is not popped
     * until emit has returned: the sink is handed a pointer Lua still owns. */
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

/* Finish setup once rt->L is populated (or NULL on failure). */
/* Restrict the global environment for the trusted model (#11): drop the
 * surfaces that reach the host filesystem, spawn processes, or load native
 * code, and expose the `catnip` namespace as the injection point apps and
 * later namespaces (ui/service/...) bind onto. Safe stdlib (string, table,
 * math, ...) is kept. */
static void harden_env(catnip_rt *rt)
{
    lua_State *L = rt->L;

    /* Whole libraries / globals that are unsafe or filesystem/native-code. */
    static const char *const nuke[] = {"io",       "package", "require", "dofile",
                                       "loadfile", "debug",   NULL};
    for (int i = 0; nuke[i]; i++) {
        lua_pushnil(L);
        lua_setglobal(L, nuke[i]);
    }

    /* Keep os time helpers, drop the dangerous members. */
    lua_getglobal(L, "os");
    if (lua_istable(L, -1)) {
        static const char *const os_nuke[] = {"execute", "exit",   "remove",    "rename",
                                              "tmpname", "getenv", "setlocale", NULL};
        for (int i = 0; os_nuke[i]; i++) {
            lua_pushnil(L);
            lua_setfield(L, -2, os_nuke[i]);
        }
    }
    lua_pop(L, 1); /* os */

    /* The catnip namespace: where apps hang catnip.view/on_open and where the
     * ui/service/... tables get bound. Empty stub for now. */
    lua_newtable(L);
    lua_pushstring(L, "0.1");
    lua_setfield(L, -2, "version");
    lua_pushstring(L, "1.0");
    lua_setfield(L, -2, "api");
    lua_setglobal(L, "catnip");
}

static catnip_rt *rt_finish(catnip_rt *rt)
{
    if (!rt) return NULL;
    if (!rt->L) {
        free(rt);
        return NULL;
    }
    luaL_openlibs(rt->L);
    install_print(rt);
    harden_env(rt);
    return rt;
}

catnip_rt *catnip_rt_new(void)
{
    catnip_rt *rt = (catnip_rt *)calloc(1, sizeof(*rt));
    if (!rt) return NULL;
    rt->L = luaL_newstate();
    return rt_finish(rt);
}

catnip_rt *catnip_rt_new_alloc(catnip_alloc_fn alloc, void *alloc_ud)
{
    if (!alloc) return catnip_rt_new();
    catnip_rt *rt = (catnip_rt *)calloc(1, sizeof(*rt));
    if (!rt) return NULL;
    /* lua_Alloc has the same shape as catnip_alloc_fn. */
    rt->L = lua_newstate((lua_Alloc)alloc, alloc_ud);
    return rt_finish(rt);
}

catnip_rt *catnip_rt_new_tracked(void)
{
    catnip_rt *rt = (catnip_rt *)calloc(1, sizeof(*rt));
    if (!rt) return NULL;
    catnip_alloc_init(&rt->acct);
    rt->tracked = 1;
    rt->L = lua_newstate(catnip_alloc_cb, &rt->acct);
    return rt_finish(rt);
}

int catnip_rt_mem(catnip_rt *rt, size_t *in_use, size_t *peak)
{
    if (!rt || !rt->tracked) return 0;
    if (in_use) *in_use = rt->acct.in_use;
    if (peak) *peak = rt->acct.peak;
    return 1;
}

void catnip_rt_set_log(catnip_rt *rt, catnip_log_fn fn, void *ud)
{
    if (!rt) return;
    rt->log = fn;
    rt->log_ud = ud;
}

lua_State *catnip_rt_lua(catnip_rt *rt)
{
    return rt ? rt->L : NULL;
}

void catnip_rt_report_error(catnip_rt *rt, lua_State *L)
{
    if (!rt || !L) return;
    const char *msg = lua_tostring(L, -1);
    if (msg == NULL) msg = "(non-string error)";
    luaL_traceback(L, L, msg, 1); /* pushes the traceback string */
    size_t len;
    const char *tb = lua_tolstring(L, -1, &len);
    emit(rt, tb, len);
    lua_pop(L, 2); /* traceback + original error */
}

void catnip_rt_log(catnip_rt *rt, const char *msg)
{
    if (!rt || !msg) return;
    emit(rt, msg, strlen(msg));
}

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
    int base = lua_gettop(L); /* the loaded function */
    lua_pushcfunction(L, msgh);
    lua_insert(L, base); /* msgh below the function */
    int st = lua_pcall(L, 0, 0, base);
    lua_remove(L, base); /* remove msgh */
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
