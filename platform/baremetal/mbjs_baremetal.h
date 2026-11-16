/*
 * mbjs_baremetal.h — MBJS (libmochiberry) bootstrap for bare-metal / custom
 * OS targets (ARM Cortex-M, RISC-V MCUs, ESP32 without an OS, hobby OSes).
 *
 * The contract with the target:
 *
 *   - The platform provides one RAM region (e.g. a linker-script section or
 *     a static buffer). The whole engine lives inside it: the mbjs-mem arena
 *     carves every allocation (engine C objects, JS heap, handle records)
 *     from the region and reclaims everything in bulk on destroy. There is
 *     no malloc, no free, no GC for the C side — "memory management
 *     kosongan", ready to be mounted on any custom OS.
 *
 *   - The adapter itself is plain C with no OS/libc requirements on the
 *     memory path. The engine core (mbjs.c) still expects a C library
 *     (newlib / picolibc / musl) for string/math helpers on the target —
 *     that is a toolchain concern, not a runtime one.
 *
 *   - std/os/console helpers (mbjs-libc) are NOT initialized: bare-metal has
 *     no filesystem, no clock, no console unless the embedder provides one.
 *     The embedder installs its own console/print functions on the context
 *     before evaluating scripts.
 */

#ifndef MBJS_BAREMETAL_H
#define MBJS_BAREMETAL_H

#include <stddef.h>

#include "baremetal_config.h"
#include "mbjs-mem.h"
#include "mbjs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MBJSBaremetal {
    MBJSMemArena arena;   /* naked region arena (the whole memory budget) */
    MBJSRuntime *rt;
    MBJSContext *ctx;
    size_t heap_limit;    /* arena limit actually applied (bytes) */
    int ready;
} MBJSBaremetal;

/* Boot the engine inside `region` (caller-owned, at least
 * MBJS_BAREMETAL_MIN_REGION bytes, alignment MBJS_MEM_ALIGN). heap_limit 0
 * selects MBJS_BAREMETAL_HEAP_LIMIT (0 -> whole region). Returns 0 on
 * success, -1 if the region is too small or the engine cannot boot. */
int mbjs_baremetal_init(MBJSBaremetal *b, void *region, size_t region_size,
                        size_t heap_limit);

/* Evaluate a JS source string in the global scope. Returns 0 on success,
 * -1 on exception (the exception value is left in the context). */
int mbjs_baremetal_eval(MBJSBaremetal *b, const char *src, size_t len);

/* Tear the engine down and rewind the region so it can be re-seeded. */
void mbjs_baremetal_destroy(MBJSBaremetal *b);

#ifdef __cplusplus
}
#endif

#endif /* MBJS_BAREMETAL_H */
