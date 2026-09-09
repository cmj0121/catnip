/* catnip_sched.c - see catnip_sched.h. */
#include "catnip_sched.h"

#include <stdlib.h>
#include <string.h>

#include "catnip_render.h"

#include "lauxlib.h"
#include "lua.h"

/* Watchdog: a script that never yields (e.g. `while true do end`) would keep
 * lua_resume from returning and freeze the whole device. A count hook fires
 * every CATNIP_WD_INSTR_PER_HOOK VM instructions; if a single resume burns more
 * than CATNIP_WD_MAX_HOOKS_PER_SLICE of them without yielding, the script is
 * aborted with an error instead of hanging. The counter is reset each resume,
 * so a well-behaved app that yields via sys.sleep is never affected. */
#ifndef CATNIP_WD_INSTR_PER_HOOK
#define CATNIP_WD_INSTR_PER_HOOK 1000
#endif
#ifndef CATNIP_WD_MAX_HOOKS_PER_SLICE
#define CATNIP_WD_MAX_HOOKS_PER_SLICE 500 /* ~500k instructions per resume */
#endif

struct catnip_sched {
    catnip_rt *rt;
    lua_State *co;          /* the app coroutine (NULL until start) */
    int co_ref;             /* registry ref pinning `co` against GC */
    int state;              /* last CATNIP_* state */
    unsigned long wake;     /* millis at which to resume, when sleeping */
    int started;            /* has the coroutine been resumed at least once */
    int exit_asked;         /* the app called sys.exit(); see l_sys_exit */
    unsigned long wd_count; /* watchdog: hook fires in the current resume */
    catnip_now_fn now;
    catnip_pump_fn pump;
    void *ud;
};

/* Runs every CATNIP_WD_INSTR_PER_HOOK instructions on the app coroutine. The
 * scheduler pointer is stashed in the coroutine's extra space. */
static void wd_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    catnip_sched *s = *(catnip_sched **)lua_getextraspace(L);
    if (!s) return;
    if (++s->wd_count > CATNIP_WD_MAX_HOOKS_PER_SLICE) {
        luaL_error(L, "catnip: script ran too long without yielding and was stopped");
    }
}

static unsigned long sched_now(catnip_sched *s)
{
    return s->now ? s->now(s->ud) : 0;
}

/* sys.sleep(ms): yield the requested delay back to the scheduler. Does not
 * block; the scheduler decides when to resume. */
static int l_sys_sleep(lua_State *co)
{
    lua_Integer ms = luaL_checkinteger(co, 1);
    if (ms < 0) ms = 0;
    lua_settop(co, 0);
    lua_pushinteger(co, ms); /* the single value handed to the scheduler */
    return lua_yield(co, 1);
}

/* sys.now(): monotonic milliseconds, via the injected clock. */
static int l_sys_now(lua_State *L)
{
    catnip_sched *s = (catnip_sched *)lua_touserdata(L, lua_upvalueindex(1));
    lua_pushinteger(L, (lua_Integer)sched_now(s));
    return 1;
}

/* sys.exit(): this app has finished.
 *
 * An app could already leave by declining short B - the platform takes it and
 * exits - but that is the user leaving, not the app finishing. A setter that
 * has written what it was opened to write is done, and had no way to say so:
 * it could only sit there waiting to be dismissed from something it had already
 * completed.
 *
 * It is a request rather than an exit. The app is running inside a coroutine
 * the shell owns, and tearing that down from inside a call on it is the same
 * mistake as a handler destroying the tree it is running on. The flag is read
 * at a safe point, exactly as the launcher's pick latch is. */
static int l_sys_exit(lua_State *L)
{
    catnip_sched *s = (catnip_sched *)lua_touserdata(L, lua_upvalueindex(1));
    if (s) s->exit_asked = 1;
    return 0;
}

static void install_sys(catnip_sched *s)
{
    lua_State *L = catnip_rt_lua(s->rt);
    lua_newtable(L); /* sys */

    lua_pushcfunction(L, l_sys_sleep);
    lua_setfield(L, -2, "sleep");

    lua_pushlightuserdata(L, s);
    lua_pushcclosure(L, l_sys_exit, 1);
    lua_setfield(L, -2, "exit");

    lua_pushlightuserdata(L, s);
    lua_pushcclosure(L, l_sys_now, 1);
    lua_setfield(L, -2, "now");

    lua_setglobal(L, "sys");
}

catnip_sched *catnip_sched_new(catnip_rt *rt, catnip_now_fn now, catnip_pump_fn pump,
                               void *ud)
{
    if (!rt) return NULL;
    catnip_sched *s = (catnip_sched *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->rt = rt;
    s->now = now;
    s->pump = pump;
    s->ud = ud;
    s->state = CATNIP_IDLE;
    s->co_ref = LUA_NOREF;
    install_sys(s);
    return s;
}

static void drop_coroutine(catnip_sched *s)
{
    if (s->co_ref != LUA_NOREF) {
        luaL_unref(catnip_rt_lua(s->rt), LUA_REGISTRYINDEX, s->co_ref);
        s->co_ref = LUA_NOREF;
    }
    s->co = NULL;
}

int catnip_sched_take_exit(catnip_sched *s)
{
    int asked;

    if (!s) return 0;
    asked = s->exit_asked;
    s->exit_asked = 0;
    return asked;
}

int catnip_sched_start(catnip_sched *s, const char *code, const char *chunkname)
{
    if (!s || !code) return -1;
    lua_State *L = catnip_rt_lua(s->rt);

    drop_coroutine(s);

    s->co = lua_newthread(L);
    /* pin the thread so it is not collected while it runs */
    s->co_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    /* arm the watchdog on the coroutine (reachable from wd_hook) */
    *(catnip_sched **)lua_getextraspace(s->co) = s;
    lua_sethook(s->co, wd_hook, LUA_MASKCOUNT, CATNIP_WD_INSTR_PER_HOOK);

    int ld = luaL_loadbuffer(s->co, code, strlen(code), chunkname ? chunkname : "=app");
    if (ld != LUA_OK) {
        /* surface the load error through the runtime, then discard */
        lua_xmove(s->co, L, 1); /* move error to L (unused) */
        lua_pop(L, 1);
        drop_coroutine(s);
        s->state = CATNIP_IDLE;
        return ld;
    }
    s->started = 0;
    s->wake = 0;
    s->state = CATNIP_SLEEP; /* due immediately on the next step */
    return 0;
}

static int resume_app(catnip_sched *s)
{
    lua_State *L = catnip_rt_lua(s->rt);
    int nres = 0;
    s->wd_count = 0; /* fresh watchdog budget for this resume */
    int r = lua_resume(s->co, L, 0, &nres);
    s->started = 1;

    if (r == LUA_YIELD) {
        /* sys.sleep yielded the requested ms as the single value. */
        lua_Integer ms = (nres >= 1) ? lua_tointeger(s->co, -nres) : 0;
        lua_pop(s->co, nres);
        s->wake = sched_now(s) + (unsigned long)ms;
        s->state = CATNIP_SLEEP;
    } else if (r == LUA_OK) {
        lua_pop(s->co, nres);
        s->state = CATNIP_DONE;
    } else {
        /* error: report through the runtime's log path */
        catnip_rt_report_error(s->rt, s->co);
        drop_coroutine(s);
        s->state = CATNIP_ERROR;
    }
    return s->state;
}

int catnip_sched_step(catnip_sched *s)
{
    if (!s) return CATNIP_IDLE;
    if (s->state == CATNIP_DONE || s->state == CATNIP_ERROR || s->state == CATNIP_IDLE)
        return s->state;

    /* Sleeping and not yet due: pump the host and wait. */
    if (s->started && (long)(s->wake - sched_now(s)) > 0) {
        if (s->pump) s->pump(s->ud);
        return CATNIP_SLEEP;
    }

    /* Due (or first run): resume the app. */
    return resume_app(s);
}

int catnip_sched_dispatch(void *ud, catnip_rt *rt, int node_ref, const char *event,
                          int index)
{
    catnip_sched *s = (catnip_sched *)ud;
    lua_State *L = catnip_rt_lua(rt);
    if (!L || node_ref == LUA_NOREF) return -1;

    lua_State *co = lua_newthread(L);
    int co_ref = luaL_ref(L, LUA_REGISTRYINDEX); /* pin it for the resume */
    if (s) {
        *(catnip_sched **)lua_getextraspace(co) = s;
        lua_sethook(co, wd_hook, LUA_MASKCOUNT, CATNIP_WD_INSTR_PER_HOOK);
        s->wd_count = 0; /* a fresh budget, exactly as a resume of the app gets */
    }

    /* ui.fire(node, event, index) rather than reaching into the node's handlers
     * here: one path into the tree, and it is the path ui.fire already
     * documents. The sentinel becomes nil rather than -1, so a handler tests
     * `if index then` and never has to know what "no row" is spelled as in C. */
    lua_getglobal(co, "ui");
    lua_getfield(co, -1, "fire");
    lua_remove(co, -2);
    lua_rawgeti(co, LUA_REGISTRYINDEX, node_ref);
    lua_pushstring(co, event);
    if (index == CATNIP_INDEX_NONE) lua_pushnil(co);
    else lua_pushinteger(co, (lua_Integer)index + 1); /* one-based, like Lua */

    int nres = 0;
    int r = lua_resume(co, L, 3, &nres);
    int rc = 0;
    if (r == LUA_YIELD) {
        rc = -1;
        lua_pushliteral(co, "catnip: a ui handler cannot yield yet - sys.sleep and "
                            "anything else that waits is not available inside on_* "
                            "(the handler runs on a one-shot coroutine; #48)");
        catnip_rt_report_error(rt, co);
    } else if (r != LUA_OK) {
        rc = -1;
        catnip_rt_report_error(rt, co);
    } else {
        /* ui.fire answers `ran, result`. A handler that ran and returned
         * something truthy is the claim the shell reads after the drain; a
         * missing handler is `false` and claims nothing, which is what makes an
         * app with no on_back the ordinary case rather than an error. */
        if (nres >= 1 && lua_toboolean(co, 1))
            rc = (nres >= 2 && lua_toboolean(co, 2)) ? 1 : 0;
    }

    luaL_unref(L, LUA_REGISTRYINDEX, node_ref); /* the dispatcher owns this ref */
    luaL_unref(L, LUA_REGISTRYINDEX, co_ref);
    return rc;
}

void catnip_sched_free(catnip_sched *s)
{
    if (!s) return;
    drop_coroutine(s);
    free(s);
}
