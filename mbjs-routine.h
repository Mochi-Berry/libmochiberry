/* mbjs-routine.h - MochiRoutine: fiber-based, cooperative, zero-pause green threads.
 *
 * MochiRoutine is the MochiBerry green-thread layer. It is deliberately a
 * better fit for the engine than stackful coroutines / goroutines:
 *
 *   * Fibers (OS-backed, fixed small stack) instead of growable goroutine stacks:
 *     no stack grow/shrink bookkeeping, no per-routine GC interaction, lower
 *     memory footprint per routine.
 *   * Cooperative, zero-pause scheduling: a routine runs until it explicitly
 *     yields; there is no preemption and no polling. A routine that never yields
 *     completes in a single slice with NO context switch at all (zero-pause).
 *   * A context switch is a single user-mode stack swap (no system call, no
 *     kernel lock), so yield/resume is on the order of tens of nanoseconds.
 *   * Deterministic ordering (cooperative) instead of goroutine-style async
 *     preemption, which makes timing-dependent JS programs reproducible.
 *
 * Backend: Win32 fibers (fibersapi.h) on Windows; a portable, deterministic
 * cooperative fallback elsewhere.
 */
#ifndef MBJS_ROUTINE_H
#define MBJS_ROUTINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MBJSRoutine MBJSRoutine;
typedef int (*MBJSRoutineFn)(MBJSRoutine *self, void *arg);

enum {
    MBJS_ROUTINE_READY     = 0,
    MBJS_ROUTINE_SUSPENDED = 1,
    MBJS_ROUTINE_DONE      = 2,
};

/* Create a routine with the given (or default, if 0) fixed stack size. */
MBJSRoutine *mbjs_routine_new(size_t stack_size);
void mbjs_routine_free(MBJSRoutine *r);

/* Run `fn(arg)` inside the routine. The current (host) fiber blocks until the
 * routine returns or yields. Zero-pause: if the host is the only active
 * routine, `fn` is invoked directly on the host stack (no context switch). */
int mbjs_routine_start(MBJSRoutine *r, MBJSRoutineFn fn, void *arg);

/* Yield the currently-running routine back to its scheduler/host. Returns after
 * the routine is resumed. Zero-pause: a no-op when only one routine is active. */
int mbjs_routine_yield(MBJSRoutine *self);

/* Resume a suspended routine. */
int mbjs_routine_resume(MBJSRoutine *r);

int mbjs_routine_status(MBJSRoutine *r);
int mbjs_routine_active(void);

/* Cooperative round-robin: drive every routine in `rs` until all are done. */
int mbjs_routine_run_all(MBJSRoutine **rs, int n, MBJSRoutineFn *fns, void **args);

#ifdef __cplusplus
}
#endif

#endif /* MBJS_ROUTINE_H */