/*
 * mbjs-mulch.h — libmulchberry: the four-mode memory manager for MBJS.
 *
 * libmulchberry is the allocation *policy* layered above the deterministic
 * mbjs-mem foundation. It manages caller-visible blocks in large "mulch beds"
 * (chunks) and offers four selectable modes, from fully automatic to fully
 * manual, on the same allocation engine:
 *
 *   MBJS_MULCH_AUTO       Full automatic. A generational mark-sweep reclaims
 *                         unmarked young allocations; survivors are promoted
 *                         to the old generation. The host marks live roots
 *                         with mbjs_mulch_mark() (mirroring a tracing GC, but
 *                         with the collector callable and bounded). When a
 *                         budget is configured, collection is triggered
 *                         automatically at the start of an allocation.
 *
 *   MBJS_MULCH_MEMORY     "Mulch Memory". Manual control with recycling:
 *                         freed blocks are zeroed and kept in the mulch bed
 *                         for immediate reuse, and mbjs_mulch_compost()
 *                         returns empty beds to the system. Deterministic,
 *                         no collector, best for long-running servers.
 *
 *   MBJS_MULCH_SEMI_AUTO  Semi-automatic / semi-manual. Allocation is
 *                         explicit, but mbjs_mulch_scope_begin() /
 *                         mbjs_mulch_scope_end() bound a region whose
 *                         allocations are reclaimed automatically even when
 *                         the application forgets to free them. Explicit
 *                         mbjs_mulch_free() is still honoured. Scopes nest.
 *
 *   MBJS_MULCH_MANUAL     Full manual, C-style: alloc/free only. Freed blocks
 *                         are reused; nothing is ever reclaimed behind the
 *                         application's back.
 *
 * All modes share one storage engine, so switching modes does not change the
 * ABI or the block layout. The manager is single-threaded, matching the
 * engine; external locking is the embedder's concern.
 *
 * The manager can back the engine itself through mbjs_mulch_mf(): pass the
 * MBJSMulch pointer as the opaque to MBJS_NewRuntime2(). AUTO mode used as an
 * engine allocator requires the embedder to mark engine roots each cycle;
 * MANUAL, MEMORY and SEMI_AUTO do not need marking.
 */

#ifndef MBJS_MULCH_H
#define MBJS_MULCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mbjs.h" /* for MBJSMallocFunctions */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MBJSMulchMode {
    MBJS_MULCH_AUTO = 0,  /* full automatic: generational mark-sweep        */
    MBJS_MULCH_MEMORY,    /* mulch memory: manual + recycling + compost     */
    MBJS_MULCH_SEMI_AUTO, /* semi-automatic: explicit alloc + scope reclaim */
    MBJS_MULCH_MANUAL,    /* full manual: alloc/free only                   */
} MBJSMulchMode;

typedef struct MBJSMulch MBJSMulch;

/* Smallest guaranteed alignment of a returned block. */
#define MBJS_MULCH_ALIGN 16

/* Default bed (chunk) size and AUTO collection threshold. */
#define MBJS_MULCH_DEFAULT_BED_SIZE (64 * 1024)
#define MBJS_MULCH_DEFAULT_THRESHOLD (1024 * 1024)

/* Create/destroy a manager. mode selects the reclamation policy. Returns NULL
 * on allocation failure. */
MBJSMulch *mbjs_mulch_new(MBJSMulchMode mode);
void mbjs_mulch_destroy(MBJSMulch *m);

MBJSMulchMode mbjs_mulch_mode(const MBJSMulch *m);
const char *mbjs_mulch_mode_name(MBJSMulchMode mode);
const char *mbjs_mulch_mode_name_of(const MBJSMulch *m);

/* Allocation. mbjs_mulch_alloc(NULL-sized) still returns a unique block.
 * Returns NULL only when the system allocator fails. */
void *mbjs_mulch_alloc(MBJSMulch *m, size_t size);
void *mbjs_mulch_calloc(MBJSMulch *m, size_t count, size_t size);
void *mbjs_mulch_realloc(MBJSMulch *m, void *ptr, size_t size);
void mbjs_mulch_free(MBJSMulch *m, void *ptr);
size_t mbjs_mulch_usable_size(const void *ptr);

/* Configure the bed size used for new chunks (0 restores the default). */
void mbjs_mulch_set_bed_size(MBJSMulch *m, size_t bytes);

/* AUTO: mark `ptr` as live for the next collection cycle. Marking an already
 * freed or foreign pointer is ignored. */
void mbjs_mulch_mark(MBJSMulch *m, void *ptr);

/* AUTO: configure the automatic trigger. 0 disables automatic collection
 * (collection then only happens on an explicit call). */
void mbjs_mulch_set_threshold(MBJSMulch *m, size_t bytes);

/* AUTO: collect the young generation — unmarked young blocks are freed and
 * marked young blocks are promoted to the old generation. */
void mbjs_mulch_collect(MBJSMulch *m);

/* AUTO: collect young *and* old generations. Unmarked blocks of both are
 * freed; marked blocks survive and lose their mark. */
void mbjs_mulch_collect_full(MBJSMulch *m);

/* SEMI_AUTO: open/close an automatic reclamation scope. mbjs_mulch_scope_end()
 * frees every block allocated in that scope (or a nested one) that is still
 * live. Returns the scope id (>= 1), or 0 for modes without scopes. */
uint64_t mbjs_mulch_scope_begin(MBJSMulch *m);
void mbjs_mulch_scope_end(MBJSMulch *m, uint64_t scope);
uint64_t mbjs_mulch_scope_current(const MBJSMulch *m);

/* MEMORY (and any mode): release empty beds back to the system allocator.
 * Cells that are still live are preserved. */
void mbjs_mulch_compost(MBJSMulch *m);

/* Run the ownership/consistency audit: returns the number of live blocks whose
 * header magic does not match (always 0 for a healthy manager). */
size_t mbjs_mulch_audit(const MBJSMulch *m);

typedef struct MBJSMulchStats {
    MBJSMulchMode mode;
    size_t live_cells;      /* blocks currently live                       */
    size_t live_bytes;      /* payload bytes of live blocks                */
    size_t reserved_bytes;  /* capacity of all beds                        */
    size_t peak_live_bytes; /* high-water mark of live_bytes               */
    size_t beds;            /* number of chunks currently held             */
    size_t alloc_calls;     /* successful allocations                      */
    size_t free_calls;      /* explicit frees                              */
    size_t recycled;        /* allocations served from a reused block      */
    size_t collected;       /* blocks reclaimed by collect/scope/compost   */
    size_t threshold;       /* AUTO trigger budget, 0 when disabled        */
} MBJSMulchStats;

MBJSMulchStats mbjs_mulch_stats(const MBJSMulch *m);

/* MBJSMallocFunctions view of a manager, for MBJS_NewRuntime2(). The opaque
 * pointer passed to the runtime must be the MBJSMulch*. */
const MBJSMallocFunctions *mbjs_mulch_mf(void);

#ifdef __cplusplus
}
#endif

#endif /* MBJS_MULCH_H */
