/*
 * mbjs-jit.h — just-in-time compilation foundation for MBJS (libmochiberry).
 *
 * MBJS is an interpreter + bytecode engine by design (deterministic, tiny,
 * embeddable — the right choice for Berry OS / bare-metal). For hosts that
 * want Bun/Clang-style native speed, this module adds a *pluggable JIT tier*
 * on top of the interpreter without touching the engine core:
 *
 *   - A "compiled function" is a plain C function pointer with the same
 *     signature as MBJS_Call(): (ctx, this_val, argc, argv) -> MBJSValue.
 *
 *   - The built-in backend (MBJS_JIT_BACKEND_CC) compiles a JS source string
 *     to native machine code *at runtime* — the source is wrapped in a tiny C
 *     shim, compiled with the system C compiler (-O2), and loaded with
 *     dlopen(). This is a real compile-on-first-use JIT pipeline; it is how
 *     you get JIT on any host that has a C toolchain.
 *
 *   - On bare-metal / custom OSes there is no compiler and no dlopen, so the
 *     backend resolves to MBJS_JIT_BACKEND_NONE and mbjs_jit_compile()
 *     returns NULL; the caller falls back to the interpreter. The API is the
 *     same either way — that is the point (see mbjs_jit_call()).
 *
 *   - mbjs_jit_observe() is the hot-function tracker: count calls, and once a
 *     function crosses the threshold it is compiled once and its native
 *     pointer is returned for subsequent direct calls. Stats tell you how
 *     many calls actually ran natively.
 *
 * A backend is intentionally NOT wired into the interpreter dispatch loop:
 * embedders choose per-function or per-module where native speed pays off,
 * which keeps worst-case behavior (allocation, GC pauses, code size)
 * predictable on constrained devices.
 */

#ifndef MBJS_JIT_H
#define MBJS_JIT_H

#include <stddef.h>

#include "mbjs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MBJSJitBackend {
    MBJS_JIT_BACKEND_AUTO = 0, /* resolve to CC when a toolchain is available */
    MBJS_JIT_BACKEND_NONE,     /* interpreter only; compilation always fails   */
    MBJS_JIT_BACKEND_CC,       /* runtime C compilation + dlopen               */
} MBJSJitBackend;

/* A JIT-compiled function. Same contract as MBJS_Call(); the return value is
 * owned by the caller (free it with MBJS_FreeValue()). */
typedef MBJSValue (*MBJSJitFn)(MBJSContext *ctx, MBJSValue this_val,
                               int argc, MBJSValue *argv);

typedef struct MBJSJitStats {
    unsigned long compile_attempts; /* compile() / observe() compilations     */
    unsigned long compile_ok;       /* ... that produced native code          */
    unsigned long compile_failed;   /* ... that failed cleanly                */
    unsigned long native_calls;     /* calls dispatched to native code        */
    unsigned long interp_calls;     /* calls that fell back to the interpreter*/
    unsigned long hot_tracked;      /* functions entered into the hot table   */
    unsigned long hot_compiled;     /* hot functions that got compiled        */
    unsigned long cache_hits;       /* mbjs_jit_call() hits the in-memory fn cache */
    unsigned long cache_disk_hits;  /* persistent cache: loaded a cached .so  */
    unsigned long cache_disk_misses;/* persistent cache: compiled and stored */
    unsigned long cache_disk_writes;/* persistent cache: .so stored on disk  */
    unsigned long cache_disk_errors;/* persistent cache: store/load failures */
} MBJSJitStats;

typedef struct MBJSJitOptions {
    const char *cc;        /* compiler binary; NULL = $CC, then "cc"          */
    const char *cc_flags;  /* extra flags; NULL = "-O2 -fPIC" (shared added)  */
    const char *inc_dir;   /* dir containing mbjs.h; NULL = "dist/include"    */
    const char *lib_dir;   /* dir containing libmochiberry.so; NULL = "dist"   */
    const char *tmp_dir;   /* scratch dir; NULL = $TMPDIR, else /tmp          */
    /* Persistent compilation cache: a directory where compiled .so files are
     * kept across processes (keyed by source + engine version + cc flags, so
     * the cache self-invalidates). On a cache hit the next boot dlopens the
     * native code directly and skips cc entirely — this is what makes the
     * JIT tier pay the toolchain cost once per device, not once per start.
     * NULL/empty = disabled (current behaviour). */
    const char *cache_dir;
    unsigned hot_threshold;/* observe() compile threshold; 0 = 100            */
} MBJSJitOptions;

typedef struct MBJSJit MBJSJit;

/* Create a JIT with the requested backend. AUTO resolves to CC only when a C
 * compiler and dlopen are actually available (never on bare-metal builds).
 * `opts` may be NULL for defaults. Returns NULL only on allocation failure. */
MBJSJit *mbjs_jit_new(MBJSJitBackend backend, const MBJSJitOptions *opts);

void mbjs_jit_free(MBJSJit *jit);

/* Resolved backend name: "cc", "none". */
const char *mbjs_jit_backend_name(const MBJSJit *jit);

/* Last error message from a failed compile (empty string if none). The
 * returned pointer is owned by the JIT. */
const char *mbjs_jit_last_error(const MBJSJit *jit);

/* Compile `source` now. `source` is a JS expression or script that evaluates
 * to a callable (e.g. "(function (n) { return n * 2; })"). Returns the
 * native function, or NULL on failure (backend "none", no toolchain, compile
 * error — check mbjs_jit_last_error()). The pointer is owned by the JIT and
 * stays valid until mbjs_jit_free(). */
MBJSJitFn mbjs_jit_compile(MBJSJit *jit, MBJSContext *ctx, const char *source,
                           const char *name);

/* Compile-or-fallback call: dispatch to native code when a compilation
 * succeeds, otherwise evaluate + call through the interpreter. Always
 * returns a valid call result (or an exception value).
 *
 * The compiled function is cached by (source, name), so repeated calls with
 * the same source do NOT recompile: the first call pays the cc+dlopen cost,
 * every later call dispatches straight to native code (stats.cache_hits
 * counts them). The cache is bounded (MBJS_JIT_CALL_CACHE_MAX entries) so
 * hot loops stay native without unbounded memory growth on devices. */
MBJSValue mbjs_jit_call(MBJSJit *jit, MBJSContext *ctx, const char *source,
                        const char *name, MBJSValue this_val, int argc,
                        MBJSValue *argv);

/* Hot-function tracker: each call bumps the counter for `name` (creating an
 * entry for `source` the first time). Once the count crosses the threshold
 * the function is compiled once and cached; subsequent observe() calls
 * return the native pointer (NULL before the threshold or if compilation
 * failed). */
MBJSJitFn mbjs_jit_observe(MBJSJit *jit, MBJSContext *ctx, const char *source,
                           const char *name);

/* Release every cached JS value held by compiled functions on `ctx` (the
 * shims keep the compiled function alive across calls). Call this before
 * MBJS_FreeContext()/MBJS_FreeRuntime() so the engine's leak checks stay
 * happy. Safe to call more than once and with "none" backends. */
void mbjs_jit_cleanup_all(MBJSJit *jit, MBJSContext *ctx);

/* Runtime statistics since creation. */
MBJSJitStats mbjs_jit_stats(const MBJSJit *jit);

#ifdef __cplusplus
}
#endif

#endif /* MBJS_JIT_H */
