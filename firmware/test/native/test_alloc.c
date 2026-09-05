/* Native test for issue #8: the accounting allocator tracks Lua's heap, grows
 * with allocation and shrinks after GC. (On the device the same allocator backs
 * onto PSRAM; here it backs onto the C heap.) */
#include <stdio.h>
#include <string.h>

#include "catnip_runtime.h"

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
    /* Untracked runtime reports no accounting. */
    catnip_rt *plain = catnip_rt_new();
    size_t x = 123, y = 123;
    CHECK(catnip_rt_mem(plain, &x, &y) == 0, "untracked runtime has no accounting");
    catnip_rt_free(plain);

    catnip_rt *rt = catnip_rt_new_tracked();
    CHECK(rt != NULL, "tracked runtime created");

    size_t base_use = 0, base_peak = 0;
    CHECK(catnip_rt_mem(rt, &base_use, &base_peak) == 1, "accounting available");
    CHECK(base_use > 0, "stdlib load already accounts for bytes");
    CHECK(base_peak >= base_use, "peak >= in_use");

    /* Allocate a big table; in_use must climb. */
    int rc = catnip_rt_dostring(rt, "big = {} for i = 1, 20000 do big[i] = i * 2 end",
                                "=grow");
    CHECK(rc == 0, "allocating script runs");

    size_t grown_use = 0, grown_peak = 0;
    catnip_rt_mem(rt, &grown_use, &grown_peak);
    CHECK(grown_use > base_use, "in_use grows after allocation");
    CHECK(grown_peak >= grown_use, "peak tracks the high-water mark");

    /* Drop it and collect; in_use must fall back down. */
    rc = catnip_rt_dostring(rt, "big = nil collectgarbage('collect')", "=free");
    CHECK(rc == 0, "gc script runs");

    size_t freed_use = 0, freed_peak = 0;
    catnip_rt_mem(rt, &freed_use, &freed_peak);
    CHECK(freed_use < grown_use, "in_use shrinks after GC");
    CHECK(freed_peak >= grown_peak,
          "peak is a monotonic high-water mark, not reset by GC");

    catnip_rt_free(rt);

    printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
