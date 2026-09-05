/* Native test for issue #11: the trusted sandbox. Dangerous surfaces are gone,
 * safe stdlib remains, and the catnip namespace exists as the injection point. */
#include <stdio.h>
#include <string.h>

#include "catnip_runtime.h"

static char g_log[256];
static void capture(void *ud, const char *msg, size_t len)
{
    (void)ud;
    if (len >= sizeof(g_log)) len = sizeof(g_log) - 1;
    memcpy(g_log, msg, len);
    g_log[len] = '\0';
}

static catnip_rt *g_rt;
/* Run `expr` (a print statement) and compare the logged line to `want`. */
static int says(const char *code, const char *want)
{
    g_log[0] = '\0';
    if (catnip_rt_dostring(g_rt, code, "=t") != 0) return 0;
    return strcmp(g_log, want) == 0;
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
    g_rt = catnip_rt_new_tracked();
    catnip_rt_set_log(g_rt, capture, NULL);

    /* Dangerous surfaces removed. */
    CHECK(says("print(type(io))", "nil"), "io is removed");
    CHECK(says("print(type(require))", "nil"), "require is removed");
    CHECK(says("print(type(package))", "nil"), "package is removed");
    CHECK(says("print(type(debug))", "nil"), "debug is removed");
    CHECK(says("print(type(loadfile))", "nil"), "loadfile is removed");
    CHECK(says("print(type(os.execute))", "nil"), "os.execute is removed");

    /* Safe pieces kept. */
    CHECK(says("print(type(os.time))", "function"), "os.time is kept");
    CHECK(says("print(type(string.format), type(math.floor), type(table.insert))",
               "function\tfunction\tfunction"),
          "safe stdlib is kept");

    /* The catnip injection point exists. */
    CHECK(says("print(type(catnip), catnip.version)", "table\t0.1"),
          "catnip namespace is present");

    catnip_rt_free(g_rt);
    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
