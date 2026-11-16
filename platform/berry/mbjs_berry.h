/*
 * mbjs_berry.h — MBJS (libmochiberry) embed adapter for Berry OS class
 * devices (ESP32, STM32, nRF52, ...).
 *
 * The adapter combines everything the fork adds on top of upstream:
 *
 *   - mbjs-mem  deterministic, non-GC arena allocator. The engine's C-side
 *              memory comes from the arena; one reset reclaims it in bulk.
 *   - hard budgets: a byte budget for the arena and a JS stack limit, so a
 *              runaway script fails cleanly instead of starving the device.
 *
 * There are no ESP-IDF/Arduino dependencies here; the adapter is plain C so
 * it can be syntax-checked on the host and cross-compiled with any toolchain.
 */

#ifndef MBJS_BERRY_H
#define MBJS_BERRY_H

#include <stddef.h>

#include "berry_config.h"
#include "mbjs-mem.h"
#include "mbjs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MBJSBerryEngine {
    MBJSMemArena arena;   /* non-GC memory foundation */
    MBJSRuntime *rt;
    MBJSContext *ctx;
    size_t heap_limit;    /* arena budget in bytes (0 = unlimited) */
    size_t stack_limit;   /* JS stack limit in bytes (0 = engine default) */
    int ready;
} MBJSBerryEngine;

/* Boot an engine. heap_limit 0 selects MBJS_BERRY_HEAP_LIMIT;
 * stack_limit 0 selects MBJS_BERRY_STACK_LIMIT. Returns 0 on success. */
int mbjs_berry_init(MBJSBerryEngine *e, size_t heap_limit, size_t stack_limit);

/* Evaluate a JS source string in the engine's global scope.
 * Returns 0 on success, -1 on exception (the exception is dumped). */
int mbjs_berry_eval(MBJSBerryEngine *e, const char *src, size_t len,
                    const char *name);

/* Evaluate a compiled bytecode blob (mbjsc -b / OTA app.mbc format) and
 * drain the event loop. This is the entry a beasm-built app module calls
 * with the blob address it embeds — the MBJS-to-BeASM bridge. */
int mbjs_berry_eval_binary(MBJSBerryEngine *e, const uint8_t *buf, size_t len);

/* Drain pending promise jobs / OS events. Returns 0 on success. */
int mbjs_berry_loop(MBJSBerryEngine *e);

/* Tear down the runtime and reclaim every arena byte. */
void mbjs_berry_destroy(MBJSBerryEngine *e);

#ifdef __cplusplus
}
#endif

#endif /* MBJS_BERRY_H */
