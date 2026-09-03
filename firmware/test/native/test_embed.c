/* Native test for issue #7: the Lua VM is embedded and runs scripts, with
 * print() and errors flowing through the log sink. Build/run with `make test`. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "catnip_runtime.h"

/* Capture the last log line the runtime emitted. */
static char g_log[1024];
static int g_lines;
static void capture(void *ud, const char *msg, size_t len)
{
    (void)ud;
    if (len >= sizeof(g_log)) len = sizeof(g_log) - 1;
    memcpy(g_log, msg, len);
    g_log[len] = '\0';
    g_lines++;
}
static void reset(void) { g_log[0] = '\0'; g_lines = 0; }

static int failures;
#define CHECK(cond, name)                                                    \
    do {                                                                     \
        if (cond) { printf("  ok   - %s\n", name); }                        \
        else { printf("  FAIL - %s\n", name); failures++; }                 \
    } while (0)

int main(void)
{
    catnip_rt *rt = catnip_rt_new();
    assert(rt && "runtime must be created");
    catnip_rt_set_log(rt, capture, NULL);

    /* hello meow: the ground-floor proof the VM runs. */
    reset();
    int rc = catnip_rt_dostring(rt, "print('meow')", "=hello");
    CHECK(rc == 0, "print('meow') runs");
    CHECK(strcmp(g_log, "meow") == 0, "print output reaches the log");

    /* real evaluation, not just string echo. */
    reset();
    rc = catnip_rt_dostring(rt, "local x = 6 * 7; print(x)", "=math");
    CHECK(rc == 0 && strcmp(g_log, "42") == 0, "arithmetic evaluates (42)");

    /* multiple args are tab-separated like stock Lua. */
    reset();
    rc = catnip_rt_dostring(rt, "print('a', 1, true)", "=multi");
    CHECK(rc == 0 && strcmp(g_log, "a\t1\ttrue") == 0, "multi-arg print");

    /* standard library is available. */
    reset();
    rc = catnip_rt_dostring(rt, "print(string.upper('meow'))", "=stdlib");
    CHECK(rc == 0 && strcmp(g_log, "MEOW") == 0, "stdlib (string) loaded");

    /* a runtime error is reported (non-zero) and its message reaches the log. */
    reset();
    rc = catnip_rt_dostring(rt, "error('boom')", "=err");
    CHECK(rc != 0, "runtime error returns non-zero");
    CHECK(strstr(g_log, "boom") != NULL, "error message reaches the log");

    /* a syntax error is caught at load time, not a crash. */
    reset();
    rc = catnip_rt_dostring(rt, "this is ) not lua", "=syntax");
    CHECK(rc != 0, "syntax error returns non-zero");

    catnip_rt_free(rt);

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
