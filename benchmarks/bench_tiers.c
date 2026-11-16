/*
 * bench_tiers.c — cross-platform three-way engine benchmark.
 *
 * Runs one identical workload through every execution tier of libmochiberry
 * so the numbers are directly comparable on the same host:
 *
 *   interp  MBJS_Eval + MBJS_Call                     (bytecode interpreter)
 *   jit     mbjs_jit_call()                            (CC backend shim;
 *                                                      cold = cc runs, warm =
 *                                                      persistent .dll cache)
 *   aot     mbjsc -e -> standalone C app               (AOT bytecode embed;
 *                                                      built by bench_tiers.sh,
 *                                                      not by this binary)
 *
 * Usage:
 *   bench_tiers interp [workload]      # workload: big | tiny
 *   bench_tiers jit    [workload]
 *
 * Environment (all optional):
 *   MBJS_JIT_CC         compiler used by the JIT shim (default: $CC, PATH)
 *   MBJS_JIT_INC_DIR    dir holding mbjs.h for the shim (default: dist/include)
 *   MBJS_JIT_LIB_DIR    ignored on Windows (shim resolves from the host exe)
 *   MBJS_JIT_CACHE_DIR  persistent shim cache; set it to measure "warm"
 *   MBJS_BENCH_WORKLOAD "tiny" selects a trivial workload (overhead probe)
 *
 * The JIT tier is a *compile-once / dispatch-cached* tier, not a native
 * codegen tier: the shim is C that calls MBJS_Eval + MBJS_Call, so its steady
 * state matches the interpreter. It wins by amortising parse/compile over
 * repeated calls and by skipping the compiler entirely once the shim is on
 * disk. Measure compile_ok=0 + disk_hits=1 to confirm you are looking at the
 * warm path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

#include "mbjs-jit.h"
#include "mbjs.h"
#include "mbjs-libc.h"

/* ---- portable timing ----------------------------------------------------- */
static double now_ms(void)
{
#if defined(_WIN32)
    /* MinGW-w64 has no clock_gettime(); QueryPerformanceCounter is the
     * monotonic equivalent. */
    LARGE_INTEGER freq, ctr;
    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&ctr))
        return 0.0;
    return (double)ctr.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

/* ---- workload ----------------------------------------------------------- */
/* Identical to examples/bench_fib.js: fib(26) + sumTo(100000) x 20. */
static const char WORK_BIG[] =
"(function(){"
"  function fib(n){ return n < 2 ? n : fib(n-1) + fib(n-2); }"
"  function sumTo(n){ var s=0; for (var i=1;i<=n;i++) s+=i; return s; }"
"  var t = 0;"
"  for (var k=0;k<20;k++) t += fib(26) + sumTo(100000);"
"  console.log(\"BENCH_RESULT \" + t);"
"  return t;"
"})";
/* Trivial workload: isolates fixed overhead (eval, dlopen, dispatch). */
static const char WORK_TINY[] =
"(function(){ return 1; })";

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "interp";
    const char *work = argc > 2 ? argv[2] : "big";
    const char *src = (strcmp(work, "tiny") == 0) ? WORK_TINY : WORK_BIG;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSValue v = MBJS_UNDEFINED;
    int64_t n = 0;
    double t0, t1, elapsed;

    rt = MBJS_NewRuntime();
    mbjs_std_init_handlers(rt);
    ctx = MBJS_NewContext(rt);
    if (!ctx) {
        printf("FAIL: no context\n");
        return 1;
    }
    /* registers the `console` global (console.log etc.) */
    mbjs_std_add_helpers(ctx, 0, NULL);

    if (strcmp(mode, "jit") == 0) {
        MBJSJitOptions opts;
        MBJSJit *jit;

        memset(&opts, 0, sizeof(opts));
        opts.cc = getenv("MBJS_JIT_CC");
        opts.inc_dir = getenv("MBJS_JIT_INC_DIR");
        opts.lib_dir = getenv("MBJS_JIT_LIB_DIR");
        {
            const char *cache = getenv("MBJS_JIT_CACHE_DIR");
            if (cache != NULL && cache[0] != '\0')
                opts.cache_dir = cache;
        }
        if (opts.inc_dir == NULL || opts.inc_dir[0] == '\0')
            opts.inc_dir = "dist/include";

        t0 = now_ms();
        jit = mbjs_jit_new(MBJS_JIT_BACKEND_CC, &opts);
        if (jit == NULL) {
            printf("FAIL: cannot create JIT\n");
            return 1;
        }
        if (strcmp(mbjs_jit_backend_name(jit), "cc") != 0) {
            printf("SKIP: JIT backend %s (%s)\n",
                   mbjs_jit_backend_name(jit), mbjs_jit_last_error(jit));
            mbjs_jit_free(jit);
            mbjs_std_free_handlers(rt);
            MBJS_FreeContext(ctx);
            MBJS_FreeRuntime(rt);
            return 0;
        }
        t1 = now_ms();
        printf("JITSETUP_MS %.1f\n", t1 - t0);

        t0 = now_ms();
        v = mbjs_jit_call(jit, ctx, src, "workload", MBJS_UNDEFINED, 0, NULL);
        t1 = now_ms();
        elapsed = t1 - t0;
        if (MBJS_IsException(v)) {
            MBJSValue e = MBJS_GetException(ctx);
            const char *s = MBJS_ToCString(ctx, e);
            printf("EXCEPTION %s\n", s ? s : "?");
            mbjs_jit_cleanup_all(jit, ctx);
            mbjs_jit_free(jit);
            mbjs_std_free_handlers(rt);
            MBJS_FreeContext(ctx);
            MBJS_FreeRuntime(rt);
            return 3;
        }
        {
            MBJSJitStats s = mbjs_jit_stats(jit);
            printf("JITSTATS compile_ok=%lu compile_failed=%lu cache_hits=%lu "
                   "disk_hits=%lu disk_misses=%lu disk_writes=%lu "
                   "disk_errors=%lu native_calls=%lu interp_calls=%lu\n",
                   s.compile_ok, s.compile_failed, s.cache_hits,
                   s.cache_disk_hits, s.cache_disk_misses, s.cache_disk_writes,
                   s.cache_disk_errors, s.native_calls, s.interp_calls);
            /* A failed JIT silently degraded to the interpreter above, which
               made "jit-cold/jit-warm" look like real numbers while nothing
               was compiled at all. Always surface the reason. */
            if (s.compile_failed > 0 || s.native_calls == 0)
                printf("JITERROR %s\n", mbjs_jit_last_error(jit));
        }
        mbjs_jit_cleanup_all(jit, ctx);
        mbjs_jit_free(jit);
    } else {
        double eval_ms;
        MBJSValue fn;
        /* interp_all times the *whole* cold path (parse+compile+run), which is
         * what a real program pays; interp times only the execution, which
         * isolates the interpreter from the parser. */
        t0 = now_ms();
        fn = MBJS_Eval(ctx, src, strlen(src), "<bench_tiers>",
                       MBJS_EVAL_TYPE_GLOBAL);
        eval_ms = now_ms() - t0;
        if (MBJS_IsException(fn)) {
            MBJSValue e = MBJS_GetException(ctx);
            const char *s = MBJS_ToCString(ctx, e);
            printf("EXCEPTION %s\n", s ? s : "?");
            mbjs_std_free_handlers(rt);
            MBJS_FreeContext(ctx);
            MBJS_FreeRuntime(rt);
            return 3;
        }
        t0 = now_ms();
        v = MBJS_Call(ctx, fn, MBJS_UNDEFINED, 0, NULL);
        t1 = now_ms();
        MBJS_FreeValue(ctx, fn);
        printf("EVAL_MS %.1f\n", eval_ms);
        if (strcmp(mode, "interp_all") == 0) {
            /* report the cold path (eval + execution) as the headline number */
            elapsed = eval_ms + (t1 - t0);
        } else {
            elapsed = t1 - t0;
        }
    }

    MBJS_ToInt64(ctx, &n, v);
    printf("RESULT %lld TIME_MS %.1f\n", (long long)n, elapsed);
    MBJS_FreeValue(ctx, v);
    fflush(stdout);
    mbjs_std_free_handlers(rt);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
    return 0;
}