/* catnip_alloc.c - see catnip_alloc.h. */
#include "catnip_alloc.h"

#include <stdlib.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include "esp_heap_caps.h"
/* heap_caps_realloc(NULL, n, cap) behaves like malloc; passing SPIRAM keeps the
 * Lua heap in external RAM. */
#define CN_REALLOC(p, n) heap_caps_realloc((p), (n), MALLOC_CAP_SPIRAM)
#define CN_FREE(p)       heap_caps_free((p))
#else
#define CN_REALLOC(p, n) realloc((p), (n))
#define CN_FREE(p)       free((p))
#endif

void catnip_alloc_init(catnip_alloc *a)
{
    if (!a) return;
    a->in_use = 0;
    a->peak = 0;
    a->ops = 0;
}

void *catnip_alloc_cb(void *ud, void *ptr, size_t osize, size_t nsize)
{
    catnip_alloc *a = (catnip_alloc *)ud;
    /* Per the Lua contract, osize is only a real byte count when ptr != NULL. */
    size_t old_bytes = (ptr == NULL) ? 0 : osize;

    if (a) a->ops++;

    if (nsize == 0) {
        CN_FREE(ptr);
        if (a) a->in_use -= old_bytes;
        return NULL;
    }

    void *np = CN_REALLOC(ptr, nsize);
    if (np == NULL) {
        /* Allocation failed: Lua keeps the old block, so accounting is unchanged. */
        return NULL;
    }
    if (a) {
        a->in_use = a->in_use - old_bytes + nsize;
        if (a->in_use > a->peak) a->peak = a->in_use;
    }
    return np;
}
