/*
 * catnip_alloc.h - a lua_Alloc that keeps Lua's heap in PSRAM and accounts for
 * it (issue #8).
 *
 * On the device the backing store is external SPIRAM, so scripts never eat into
 * the ~512 KB of internal SRAM the rest of the firmware needs. On a host it
 * falls back to the C allocator so the runtime and its tests build anywhere.
 *
 * The accounting (bytes in use, high-water mark) is the basis for the memory
 * cap a later trust model will enforce; for now it is observability only.
 */
#ifndef CATNIP_ALLOC_H
#define CATNIP_ALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t in_use;     /* bytes Lua currently holds */
    size_t peak;       /* high-water mark of in_use */
    unsigned long ops; /* number of alloc/realloc/free calls */
} catnip_alloc;

void catnip_alloc_init(catnip_alloc *a);

/* A lua_Alloc: pass as the allocator to lua_newstate, with `ud` set to a
 * catnip_alloc*. Follows the Lua contract: when `ptr` is NULL, `osize` is a
 * type tag (not a byte count) and is treated as zero for accounting. */
void *catnip_alloc_cb(void *ud, void *ptr, size_t osize, size_t nsize);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_ALLOC_H */
