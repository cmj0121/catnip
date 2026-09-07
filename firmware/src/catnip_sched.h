/*
 * catnip_sched.h - the cooperative scheduler (issue #9), the heart of the
 * runtime.
 *
 * A script runs inside a Lua coroutine. When it calls sys.sleep(ms) it *yields*
 * back to the scheduler instead of blocking; the scheduler pumps the host (on
 * the device, lv_timer_handler, so the UI stays live) and resumes the script
 * only when its wake time arrives. The script reads as plain synchronous code:
 *
 *     while true do
 *       status.text = service.http.get(url)   -- (http yields too, later)
 *       sys.sleep(5000)                       -- yields; UI keeps running
 *     end
 *
 * The scheduler is driven one step at a time from the main loop (loop() on the
 * device). Time and the pump are injected so the same code is exercised
 * deterministically by host tests.
 */
#ifndef CATNIP_SCHED_H
#define CATNIP_SCHED_H

#include "catnip_render.h"
#include "catnip_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scheduler state, returned by catnip_sched_step. */
enum {
    CATNIP_SLEEP = 0, /* script is waiting; the host was pumped */
    CATNIP_DONE = 1,  /* script returned normally */
    CATNIP_ERROR = 2, /* script raised an error (reported to the runtime log) */
    CATNIP_IDLE = 3   /* no script loaded */
};

/* Monotonic milliseconds (millis() on the device). */
typedef unsigned long (*catnip_now_fn)(void *ud);
/* Do a slice of host work while a script waits (lv_timer_handler on device). */
typedef void (*catnip_pump_fn)(void *ud);

typedef struct catnip_sched catnip_sched;

/* Create a scheduler over `rt`. `now`/`pump`/`ud` are the host hooks. Installs
 * the `sys` table (sys.sleep, sys.now) into the runtime's globals. */
catnip_sched *catnip_sched_new(catnip_rt *rt, catnip_now_fn now, catnip_pump_fn pump,
                               void *ud);

/* Load `code` as the app coroutine, ready to run on the next step. Returns 0 on
 * success, non-zero on a load (syntax) error. */
int catnip_sched_start(catnip_sched *s, const char *code, const char *chunkname);

/* Advance the app by one step: resume it if it is due, otherwise pump the host.
 * Returns one of the CATNIP_* states. */
int catnip_sched_step(catnip_sched *s);

/* A catnip_render_dispatch_fn (pass the scheduler as `ud`) that runs one ui
 * handler on a coroutine of its own, resumed exactly once, carrying the same
 * count hook an app coroutine carries. The hook is the point: a handler on the
 * main lua_State would run with no hook at all, so an on_click containing
 * `while true do end` would freeze the device and the watchdog that exists for
 * precisely that would never fire.
 *
 * A handler that yields - sys.sleep inside on_click, which is among the first
 * things anyone will write - is an error today, and the message says so in
 * those words rather than in Lua's ("attempt to yield from outside a
 * coroutine", which names nothing the author did). Making it yieldable needs
 * the scheduler to hold a queue of runnable coroutines instead of one, which is
 * this file's contract to change and not the renderer's; #48 does it, and then
 * the only thing that changes here is which function is installed. */
int catnip_sched_dispatch(void *ud, catnip_rt *rt, int node_ref, const char *event);

void catnip_sched_free(catnip_sched *s);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_SCHED_H */
