/* Native test for issue #9: the cooperative scheduler. A script that sleeps in
 * a loop must yield (not block), let the host pump between resumes, and only
 * advance when its wake time arrives. Time and the pump are faked so the whole
 * thing is deterministic. */
#include <stdio.h>

#include "catnip_runtime.h"
#include "catnip_sched.h"
#include "lauxlib.h"
#include "lua.h"

static unsigned long g_now;  /* fake monotonic clock (ms) */
static int g_pump;           /* counts host pumps (stand-in for lv_timer_handler) */

static unsigned long now_fn(void *ud) { (void)ud; return g_now; }
static void pump_fn(void *ud) { (void)ud; g_pump++; }

static int failures;
#define CHECK(cond, name)                                                    \
    do {                                                                     \
        if (cond) { printf("  ok   - %s\n", name); }                        \
        else { printf("  FAIL - %s\n", name); failures++; }                 \
    } while (0)

static lua_Integer global_int(catnip_rt *rt, const char *name)
{
    lua_State *L = catnip_rt_lua(rt);
    lua_getglobal(L, name);
    lua_Integer v = lua_tointeger(L, -1);
    lua_pop(L, 1);
    return v;
}

/* Drive the scheduler to completion; time advances only while the app waits. */
static int drive(catnip_sched *s)
{
    int st, guard = 0;
    do {
        st = catnip_sched_step(s);
        if (st == CATNIP_SLEEP) g_now += 250; /* time passes as the host pumps */
    } while (st != CATNIP_DONE && st != CATNIP_ERROR && ++guard < 10000);
    return st;
}

int main(void)
{
    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_sched *s = catnip_sched_new(rt, now_fn, pump_fn, NULL);
    CHECK(s != NULL, "scheduler created");

    /* A loop that sleeps between iterations - the canonical app shape. */
    const char *app =
        "count = 0\n"
        "for i = 1, 3 do\n"
        "  count = count + 1\n"
        "  sys.sleep(1000)\n"
        "end\n";

    g_now = 0;
    g_pump = 0;
    int rc = catnip_sched_start(s, app, "=loop");
    CHECK(rc == 0, "app loads");

    int st = drive(s);
    CHECK(st == CATNIP_DONE, "app runs to completion");
    CHECK(global_int(rt, "count") == 3, "all three iterations ran");
    CHECK(g_pump > 0, "host was pumped while the app slept (UI stays live)");
    CHECK(g_pump >= 3, "host pumped many times across the sleeps");

    /* sys.now() reflects the injected clock. */
    g_now = 12345;
    catnip_rt_dostring(rt, "SEEN = sys.now()", "=now");
    CHECK(global_int(rt, "SEEN") == 12345, "sys.now() reads the clock");

    /* A script with no sleep finishes in a single step. */
    g_pump = 0;
    catnip_sched_start(s, "done_here = true", "=quick");
    st = catnip_sched_step(s);
    CHECK(st == CATNIP_DONE, "non-sleeping app completes immediately");

    /* An app that errors is reported, not crashed. */
    catnip_sched_start(s, "sys.sleep(10) error('boom in app')", "=bad");
    g_now = 0;
    st = drive(s);
    CHECK(st == CATNIP_ERROR, "faulting app ends in ERROR state");

    catnip_sched_free(s);
    catnip_rt_free(rt);

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
