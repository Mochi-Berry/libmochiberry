/*
 * mbjs-mem.h — deterministic, non-GC memory management foundation for MBJS.
 *
 * This module is deliberately NOT a garbage collector. Memory is managed with
 * explicit ownership only:
 *
 *   - MBJSMemArena   a bump (arena) allocator built from a linked list of
 *                    blocks. Allocations are cheap, never reclaimed
 *                    individually (free is a no-op) and are released in bulk
 *                    with mbjs_mem_arena_reset()/destroy(). An optional limit
 *                    turns the arena into a hard memory budget.
 *
 *   - MBJSMemHandle  an ownership-tagged view of a pointer. Every handle is in
 *                    exactly one state:
 *                      MBJS_MEM_BORROWED  the holder may read, never free.
 *                      MBJS_MEM_TAKEN     ownership was transferred to the
 *                                         holder ("parameter diambil"); the
 *                                         holder must release it.
 *                      MBJS_MEM_FREE      released or never initialized.
 *                    Ownership moves explicitly with mbjs_mem_take() /
 *                    mbjs_mem_borrow(); nothing happens implicitly, so there is
 *                    no GC pause and no reference counting to get wrong.
 *
 *   - Live-handle registry  the arena tracks every handle that was initialized
 *                    on it. mbjs_mem_arena_audit() reports handles that were
 *                    still TAKEN (leaked) or BORROWED when the arena was reset,
 *                    which turns "forgot to free" into a test failure instead
 *                    of a silent leak.
 *
 * The arena can also back the engine itself: pass mbjs_mem_mf() with the arena
 * as the opaque to MBJS_NewRuntime2() so every byte the runtime allocates
 * comes from (and is reclaimed by) the same deterministic arena.
 *
 * Bare-metal / custom-OS mode ("memory management kosongan"):
 *
 *   - mbjs_mem_arena_init_region() seeds the arena with a caller-owned,
 *     fixed memory region. The arena then never touches the system
 *     allocator: blocks, handle records and engine allocations all come from
 *     the region, and exhaustion returns NULL. This is the bootstrap path
 *     for bare-metal targets and custom OSes that have no malloc at all
 *     (see platform/baremetal/).
 *
 *   - Compile with -DMBJS_MEM_NO_LIBC to strip every libc dependency
 *     (malloc/free/memset/memcpy). Region-backed arenas work without a libc;
 *     the no-libc object is verified by `make baremetal-check` (nm shows no
 *     malloc/free/memcpy references).
 *
 * This is a foundation, not a policy: embedders are free to keep using the
 * engine's default allocator and reserve the arena for their own native
 * modules and bytecode buffers.
 */

#ifndef MBJS_MEM_H
#define MBJS_MEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mbjs.h" /* for MBJSMallocFunctions */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Arena                                                                */
/* ------------------------------------------------------------------ */

typedef struct MBJSMemBlock {
    struct MBJSMemBlock *next;
    size_t size;   /* total block capacity in bytes (payload) */
    size_t used;   /* bytes handed out from this block */
    /* 8/16-aligned payload follows */
} MBJSMemBlock;

typedef struct MBJSMemHandleNode MBJSMemHandleNode;

typedef struct MBJSMemArena {
    MBJSMemBlock *head;              /* current (active) block */
    size_t block_size;               /* default block capacity */
    size_t total;                    /* bytes currently handed out */
    size_t peak;                     /* high-water mark of total */
    size_t limit;                    /* 0 = unlimited */
    size_t block_count;
    MBJSMemHandleNode *handles;      /* live handles registered on this arena */
    void *region;                    /* non-NULL: fixed region mode (bare-metal) */
    size_t region_size;              /* size of the fixed region, in bytes */
    uint32_t magic;                  /* MBJS_MEM_ARENA_MAGIC */
} MBJSMemArena;

#define MBJS_MEM_ARENA_MAGIC 0x4d454d41u /* "MEMA" */
#define MBJS_MEM_DEFAULT_BLOCK_SIZE (64 * 1024)

/* All arena payloads are aligned to at least this many bytes. */
#define MBJS_MEM_ALIGN (sizeof(void *) >= 8 ? 16 : 8)

/* Initialize an arena. block_size 0 selects MBJS_MEM_DEFAULT_BLOCK_SIZE. */
void mbjs_mem_arena_init(MBJSMemArena *a, size_t block_size);

/* Initialize an arena over a caller-owned fixed memory region (bare-metal /
 * custom-OS bootstrap). The arena never calls the system allocator: every
 * block, handle record and engine allocation is carved from `region` and
 * exhaustion returns NULL. size must be at least MBJS_MEM_ALIGN + one block
 * header. Returns 0 on success, -1 on invalid arguments. The region stays
 * owned by the caller; mbjs_mem_arena_destroy() only rewinds it. */
int mbjs_mem_arena_init_region(MBJSMemArena *a, void *region, size_t size);

/* Release every block and every live handle record. The arena struct itself
 * is left zeroed; call mbjs_mem_arena_init() before reusing it. */
void mbjs_mem_arena_destroy(MBJSMemArena *a);

/* Release every block and handle record but keep the arena configuration
 * (block size, limit) so the arena can be reused. */
void mbjs_mem_arena_reset(MBJSMemArena *a);

/* Allocate `size` bytes from the arena. Returns NULL (and records the
 * failure in a->total unchanged) if the arena limit would be exceeded or
 * the system is out of memory. */
void *mbjs_mem_arena_alloc(MBJSMemArena *a, size_t size);

/* Like mbjs_mem_arena_alloc() but zero-filled. */
void *mbjs_mem_arena_allocz(MBJSMemArena *a, size_t size);

/* Grow or shrink a previous arena allocation in place when possible
 * (the fast path is extending the most recent allocation), otherwise
 * allocate a fresh block and copy. */
void *mbjs_mem_arena_realloc(MBJSMemArena *a, void *ptr, size_t size);

/* Release an arena allocation. In bump mode this is a no-op; memory is
 * reclaimed in bulk by reset/destroy. */
void mbjs_mem_arena_free(MBJSMemArena *a, void *ptr);

/* Number of bytes the engine/application handed out through this arena. */
size_t mbjs_mem_arena_used(MBJSMemArena *a);

/* High-water mark of mbjs_mem_arena_used(). */
size_t mbjs_mem_arena_peak(MBJSMemArena *a);

/* Hard budget in bytes; allocations that would exceed it fail. 0 = unlimited. */
void mbjs_mem_arena_set_limit(MBJSMemArena *a, size_t limit);

/* ------------------------------------------------------------------ */
/* Ownership-marked handles                                            */
/* ------------------------------------------------------------------ */

typedef enum MBJSMemState {
    MBJS_MEM_FREE = 0,    /* released or never initialized */
    MBJS_MEM_BORROWED,    /* holder may read, must not free */
    MBJS_MEM_TAKEN,       /* ownership transferred to the holder; must release */
} MBJSMemState;

typedef struct MBJSMemHandle {
    void *ptr;
    size_t size;
    const char *owner;    /* debug tag, not owned by the handle */
    MBJSMemState state;
    uint32_t magic;       /* MBJS_MEM_HANDLE_MAGIC */
} MBJSMemHandle;

#define MBJS_MEM_HANDLE_MAGIC 0x48414e44u /* "HAND" */
#define MBJS_MEM_HANDLE_NULL { NULL, 0, NULL, MBJS_MEM_FREE, 0 }

/* Register an allocation as a live handle owned by `arena` and tagged with
 * `owner`. The handle starts in the MBJS_MEM_TAKEN state: whoever created it
 * owns it and must release it. */
MBJSMemHandle mbjs_mem_handle_init(MBJSMemArena *arena, void *ptr, size_t size,
                                   const char *owner);

/* Mark a handle as borrowed: the caller keeps ownership and the callee must
 * not free it. The handle stays live in the arena registry. */
MBJSMemHandle mbjs_mem_borrow(MBJSMemHandle h, const char *borrower);

/* Mark a handle as taken: ownership is transferred to the callee. This is the
 * "parameter diambil" marker — after this call the callee alone is
 * responsible for releasing the handle. */
MBJSMemHandle mbjs_mem_take(MBJSMemHandle h);

/* Release a handle: frees the underlying pointer through the arena (a no-op
 * in bump mode) and removes it from the live registry. Only a TAKEN handle
 * may be released; borrowed handles must be abandoned instead. */
void mbjs_mem_release(MBJSMemArena *arena, MBJSMemHandle *h);

/* Drop a borrowed (or freed) handle without freeing anything: the handle is
 * removed from the arena registry, but the underlying pointer stays alive in
 * the arena until the next reset/destroy. */
void mbjs_mem_abandon(MBJSMemArena *arena, MBJSMemHandle *h);

bool mbjs_mem_handle_valid(MBJSMemHandle h);

/* ------------------------------------------------------------------ */
/* Ownership annotations for function parameters                       */
/* ------------------------------------------------------------------ */

/*
 * These macros document the ownership contract of a function parameter.
 * They expand to nothing at runtime; tools, reviewers and the handle
 * state machine above enforce the contract they name.
 *
 *   MBJS_MEM_PARAM_TAKEN(name)     ownership of `name` transfers to the
 *                                  callee (call mbjs_mem_take() on entry).
 *   MBJS_MEM_PARAM_BORROWED(name)  `name` is borrowed; the callee must not
 *                                  free or retain it beyond the call.
 *   MBJS_MEM_PARAM_OWNED(name)     `name` is owned by the callee and must
 *                                  be released on every exit path.
 */
#define MBJS_MEM_PARAM_TAKEN(name)
#define MBJS_MEM_PARAM_BORROWED(name)
#define MBJS_MEM_PARAM_OWNED(name)

/* ------------------------------------------------------------------ */
/* Auditing                                                             */
/* ------------------------------------------------------------------ */

typedef struct MBJSMemAudit {
    size_t handles_total;   /* handles still registered at audit time */
    size_t taken;           /* ... of which still TAKEN (potential leaks) */
    size_t borrowed;        /* ... of which still BORROWED */
    size_t bytes;           /* bytes those handles cover */
} MBJSMemAudit;

/* Walk the live-handle registry and report how many handles are still open.
 * A non-zero `taken` after mbjs_mem_arena_reset() is a leak. */
MBJSMemAudit mbjs_mem_arena_audit(MBJSMemArena *a);

/* ------------------------------------------------------------------ */
/* Arena-backed allocator for MBJS_NewRuntime2()                       */
/* ------------------------------------------------------------------ */

/* Returns malloc functions that allocate through the arena passed as the
 * opaque pointer to MBJS_NewRuntime2(). frees are no-ops; memory is reclaimed
 * with mbjs_mem_arena_reset()/destroy() after MBJS_FreeRuntime(). */
const MBJSMallocFunctions *mbjs_mem_mf(void);

#ifdef __cplusplus
}
#endif

#endif /* MBJS_MEM_H */
