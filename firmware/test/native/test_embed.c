/* Native test for issue #7: the Lua VM is embedded and runs scripts, with
 * print() and errors flowing through the log sink. Build/run with `make test`. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catnip_runtime.h"

/* Capture the last log line the runtime emitted. The buffer is large enough for
 * the long lines below, and g_len records the length the sink was actually
 * handed, so a truncated line cannot pass for a whole one. */
static char g_log[8192];
static size_t g_len;
static int g_lines;
static void capture(void *ud, const char *msg, size_t len)
{
    (void)ud;
    g_len = len;
    if (len >= sizeof(g_log)) len = sizeof(g_log) - 1;
    memcpy(g_log, msg, len);
    g_log[len] = '\0';
    g_lines++;
}
static void reset(void)
{
    g_log[0] = '\0';
    g_len = 0;
    g_lines = 0;
}

/* A Lua allocator that turns "print() built its line in memory Lua had already
 * freed" into something a test can assert on: every block Lua frees is first
 * overwritten with a byte no script here prints. The block is deliberately not
 * returned to the C allocator, because a later allocation reusing it would
 * overwrite the poison before the assertion could read it. The process is
 * short-lived, so the leak is the cheap half. */
static void *poison_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;
    if (nsize == 0) {
        /* Per the Lua contract osize is only a byte count when ptr != NULL. */
        if (ptr) memset(ptr, 0xA5, osize);
        return NULL;
    }
    return realloc(ptr, nsize);
}

/* Whether every byte of the captured line is the one the script printed. A
 * bounded scan, because a line built in freed memory carries no terminator of
 * its own. */
static int all_bytes_are(const char *s, size_t n, char want)
{
    for (size_t i = 0; i < n; i++) {
        if (s[i] != want) return 0;
    }
    return 1;
}

static int failures;
#define CHECK(cond, name)                                                                \
    do {                                                                                 \
        if (cond) {                                                                      \
            printf("  ok   - %s\n", name);                                               \
        } else {                                                                         \
            printf("  FAIL - %s\n", name);                                               \
            failures++;                                                                  \
        }                                                                                \
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

    /* Issue #47: a line long enough to outgrow the print buffer. luaL_Buffer
     * starts in a LUAL_BUFFERSIZE-byte struct field (1024 bytes on a host, 512
     * on the device) and moves to a heap block held by a userdata once the line
     * passes that, and only then does it matter whether the buffer's own stack
     * bookkeeping is where the buffer thinks it is. Short lines never reach the
     * move, which is why this went unseen. Under the poisoning allocator a line
     * assembled in a block Lua has already freed arrives at the log as the
     * poison byte instead of the text, so the failure is an assertion rather
     * than a hope of catching a bad byte. */
    catnip_rt *prt = catnip_rt_new_alloc(poison_alloc, NULL);
    assert(prt && "poisoned runtime must be created");
    catnip_rt_set_log(prt, capture, NULL);

    reset();
    rc = catnip_rt_dostring(prt, "print(string.rep('x', 2000))", "=long");
    CHECK(rc == 0 && g_lines == 1, "a line past the buffer's initial size prints");
    CHECK(g_len == 2000, "the whole long line reaches the log");
    CHECK(all_bytes_are(g_log, g_len < sizeof(g_log) ? g_len : sizeof(g_log) - 1, 'x'),
          "the long line is the text, not freed memory");

    /* The same growth reached through the multi-argument path, which is what a
     * script dumping a table actually does: each argument is converted and
     * appended, and the buffer moves partway through the line. */
    reset();
    rc = catnip_rt_dostring(prt, "local s = string.rep('y', 900) print(s, s)", "=long2");
    CHECK(rc == 0 && g_len == 1801, "a long two-argument line reaches the log whole");
    CHECK(g_log[900] == '\t' && all_bytes_are(g_log, 900, 'y') &&
              all_bytes_are(g_log + 901, 900, 'y'),
          "both arguments and their separator survive the growth");

    catnip_rt_free(prt);

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
