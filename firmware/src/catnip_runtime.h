/*
 * catnip_runtime.h - embedding surface for the Catnip Lua runtime.
 *
 * Issue #7: create/close a lua_State and run a script, with all output routed
 * through a log sink (so the same code works on a host and on the device, where
 * there is no stdout). Later issues extend this state:
 *   #8  swaps in a PSRAM-backed allocator (catnip_rt_new_alloc).
 *   #9  drives it as a coroutine from the main loop (catnip_sched.*).
 */
#ifndef CATNIP_RUNTIME_H
#define CATNIP_RUNTIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lua_State;
typedef struct catnip_rt catnip_rt;

/* Sink for anything the script prints or any error the runtime reports. */
typedef void (*catnip_log_fn)(void *ud, const char *msg, size_t len);

/* Optional custom allocator (matches Lua's lua_Alloc); NULL uses the default.
 * Issue #8 passes its PSRAM allocator here. */
typedef void *(*catnip_alloc_fn)(void *ud, void *ptr, size_t osize, size_t nsize);

/* Create a runtime with the standard libraries loaded. Returns NULL on OOM. */
catnip_rt *catnip_rt_new(void);

/* Same, but with a custom allocator (see catnip_alloc_fn). */
catnip_rt *catnip_rt_new_alloc(catnip_alloc_fn alloc, void *alloc_ud);

/* Like catnip_rt_new, but with a built-in PSRAM/accounting allocator (#8).
 * Query its usage with catnip_rt_mem. */
catnip_rt *catnip_rt_new_tracked(void);

/* If the runtime was created tracked, fills *in_use and *peak (bytes) and
 * returns 1; otherwise returns 0. Either out pointer may be NULL. */
int catnip_rt_mem(catnip_rt *rt, size_t *in_use, size_t *peak);

/* Route print()/errors to `fn`. Until set, output goes to stdout. */
void catnip_rt_set_log(catnip_rt *rt, catnip_log_fn fn, void *ud);

/* Run a chunk. Returns 0 on success, non-zero on load/runtime error (the error
 * message is sent to the log). `chunkname` labels the chunk in errors. */
int catnip_rt_dostring(catnip_rt *rt, const char *code, const char *chunkname);

/* Run a script file. Returns 0 on success, non-zero on error. */
int catnip_rt_dofile(catnip_rt *rt, const char *path);

/* The underlying lua_State, for issues #8/#9 that build on it. */
struct lua_State *catnip_rt_lua(catnip_rt *rt);

/* Report the error object on top of `L` (which may be a coroutine of this
 * runtime) through the log, with a traceback. Pops the error. Used by the
 * scheduler when an app coroutine faults. */
void catnip_rt_report_error(catnip_rt *rt, struct lua_State *L);

void catnip_rt_free(catnip_rt *rt);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_RUNTIME_H */
