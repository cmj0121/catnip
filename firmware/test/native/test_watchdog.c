/* Native test for issue #10: the watchdog. A script that never yields must be
 * stopped (reported as an error), not allowed to hang the scheduler; a
 * well-behaved script must not be affected. */
#include <stdio.h>
#include <string.h>

#include "catnip_runtime.h"
#include "catnip_sched.h"

static unsigned long g_now;
static void pump_fn(void *ud) { (void)ud; }
static unsigned long now_fn(void *ud) { (void)ud; return g_now; }

static char g_log[512];
static void capture(void *ud, const char *msg, size_t len)
{
    (void)ud;
    if (len >= sizeof(g_log)) len = sizeof(g_log) - 1;
    memcpy(g_log, msg, len);
    g_log[len] = '\0';
}

static int failures;
#define CHECK(cond, name)                                                    \
    do {                                                                     \
        if (cond) { printf("  ok   - %s\n", name); }                        \
        else { printf("  FAIL - %s\n", name); failures++; }                 \
    } while (0)

int main(void)
{
    catnip_rt *rt = catnip_rt_new_tracked();
    catnip_rt_set_log(rt, capture, NULL);
    catnip_sched *s = catnip_sched_new(rt, now_fn, pump_fn, NULL);

    /* A runaway loop that never yields: the watchdog must stop it. If the
     * watchdog were absent, this step would never return. */
    g_log[0] = '\0';
    catnip_sched_start(s, "while true do end", "=runaway");
    int st = catnip_sched_step(s);
    CHECK(st == CATNIP_ERROR, "runaway script is stopped, not hung");
    CHECK(strstr(g_log, "too long") != NULL, "watchdog error is reported");

    /* A normal, finite script must not trip the watchdog. */
    catnip_sched_start(s, "total = 0 for i = 1, 100 do total = total + i end", "=ok");
    int guard = 0;
    do { st = catnip_sched_step(s); } while (st == CATNIP_SLEEP && ++guard < 100);
    CHECK(st == CATNIP_DONE, "well-behaved script completes normally");

    catnip_sched_free(s);
    catnip_rt_free(rt);

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
