/*
 * bench_jit.c — JIT-tier benchmark for MBJS (libmochiberry).
 *
 * Runs the same workload as examples/bench_fib.js through the JIT call
 * path: the first mbjs_jit_call() compiles the source with the system C
 * compiler (cc -O2 -> dlopen), every following call hits the bounded
 * call-path cache and dispatches straight to native code. Prints elapsed
 * wall time and the JIT stats (compile_ok / cache_hits / native_calls)
 * so you can see the cache working.
 *
 * Phase 2 demonstrates the persistent compilation cache (.mbjso): both
 * phases share one cache_dir, so a fresh JIT ("second boot") dlopens the
 * .so files compiled by phase 1 and skips the C compiler entirely — the
 * JIT tier pays the toolchain once per device, not once per start.
 *
 * Build & run:
 *   cc -O2 -I. -I dist/include bench_jit.c -Ldist -lmochiberry \
 *       -Wl,-rpath,'$ORIGIN/../dist' -o build/bench-jit -lm -ldl
 *   build/bench-jit
 *   (or: make bench)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "mbjs-jit.h"
#include "mbjs.h"

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

/* Create a unique scratch directory. POSIX uses mkdtemp(); Windows has no
 * mkdtemp(), so build the name from the process id + a counter. */
#if defined(_WIN32)
static char *make_temp_dir(const char *tag)
{
    static LONG counter = 0;
    char *path = malloc(512);
    char dir[512];
    if (path == NULL)
        return NULL;
    if (!GetTempPathA(sizeof(dir), dir)) {
        snprintf(dir, sizeof(dir), ".");
    }
    for (;;) {
        snprintf(path, 512, "%s%s-%lu-%ld", dir, tag,
                 (unsigned long)GetCurrentProcessId(),
                 (long)InterlockedIncrement(&counter));
        if (CreateDirectoryA(path, NULL))
            return path;
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            break;
    }
    free(path);
    return NULL;
}
#else
static char *make_temp_dir(const char *tag)
{
    char tmpl[] = "/tmp/mbjsjit-bench-XXXXXX";
    char *p = mkdtemp(tmpl);
    (void)tag;
    return p;
}
#endif

/* The JIT compiles a self-contained JS expression; recursion goes through
 * arguments.callee so the string is context-free (same as test_jit.c). */
static const char FIB_SRC[] =
    "(function (n) { return n < 2 ? n : "
    "arguments.callee(n - 1) + arguments.callee(n - 2); })";
static const char SUM_SRC[] =
    "(function (n) { var s = 0; for (var i = 1; i <= n; i++) s += i; "
    "return s; })";

#define FIB_N 26
#define SUM_N 100000
#define ITERS 20

/* Persistent JIT cache dir, shared by both phases. The array/pointer must
 * outlive both JIT instances. */
#if defined(_WIN32)
static char *CACHE_DIR = NULL;
#else
static char CACHE_DIR[] = "/tmp/mbjsjit-bench-XXXXXX";
#endif

int main(void)
{
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSJit *jit;
    MBJSJitStats stats;
    MBJSJitOptions opts;
    MBJSValue argv[1];
    int64_t total = 0;
    int64_t expect = (int64_t)ITERS * (121393LL + 5000050000LL);
    double t0, t1;
    int i, n;

    memset(&opts, 0, sizeof(opts));
    opts.cc_flags = "-O2 -fPIC";
    /* On Windows the JIT defaults inc_dir to the host exe's own directory
     * (self-contained "headers next to the exe" layout). When the bench
     * runs from a different directory, point the JIT at the header dir
     * explicitly: MBJS_JIT_INC_DIR / MBJS_JIT_LIB_DIR. */
    {
        const char *inc_env = getenv("MBJS_JIT_INC_DIR");
        const char *lib_env = getenv("MBJS_JIT_LIB_DIR");
        const char *cc_env = getenv("MBJS_JIT_CC");
        if (inc_env != NULL && inc_env[0] != '\0')
            opts.inc_dir = inc_env;
        if (lib_env != NULL && lib_env[0] != '\0')
            opts.lib_dir = lib_env;
        if (cc_env != NULL && cc_env[0] != '\0')
            opts.cc = cc_env;
    }
#if defined(_WIN32)
    /* MinGW ignores -fPIC for PE targets and warns about it; drop it. */
    opts.cc_flags = "-O2";
#endif
    /* One persistent cache dir shared by both phases: phase 1 compiles
     * and stores the shims, phase 2 ("second boot") dlopens them and
     * skips the C compiler entirely. */
#if defined(_WIN32)
    CACHE_DIR = make_temp_dir("mbjsjit-bench");
    if (CACHE_DIR == NULL) {
        printf("FAIL: cannot create cache dir\n");
        return 1;
    }
    opts.cache_dir = CACHE_DIR;
#else
    opts.cache_dir = mkdtemp(CACHE_DIR);
    if (opts.cache_dir == NULL) {
        printf("FAIL: cannot mkdtemp cache dir\n");
        return 1;
    }
#endif
    rt = MBJS_NewRuntime();
    ctx = MBJS_NewContext(rt);
    jit = mbjs_jit_new(MBJS_JIT_BACKEND_CC, &opts);
    if (jit == NULL || mbjs_jit_backend_name(jit) == NULL) {
        printf("FAIL: no JIT\n");
        return 1;
    }
    if (strcmp(mbjs_jit_backend_name(jit), "cc") != 0) {
        printf("SKIP: JIT backend unavailable (%s)\n",
               mbjs_jit_backend_name(jit));
        return 0;
    }

    t0 = now_ms();
    for (i = 0; i < ITERS; i++) {
        MBJSValue fib_r, sum_r;
        int64_t v;

        /* fib(FIB_N) via mbjs_jit_call() — first call compiles+caches,
         * the rest are cache hits. */
        argv[0] = MBJS_NewInt32(ctx, FIB_N);
        fib_r = mbjs_jit_call(jit, ctx, FIB_SRC, "fib", MBJS_UNDEFINED, 1, argv);
        MBJS_FreeValue(ctx, argv[0]);
        if (MBJS_IsException(fib_r)) {
            MBJS_FreeValue(ctx, MBJS_GetException(ctx));
            printf("FAIL: jit fib threw\n");
            return 1;
        }

        /* sumTo(SUM_N) — same path. */
        argv[0] = MBJS_NewInt32(ctx, SUM_N);
        sum_r = mbjs_jit_call(jit, ctx, SUM_SRC, "sumTo", MBJS_UNDEFINED, 1, argv);
        MBJS_FreeValue(ctx, argv[0]);
        if (MBJS_IsException(sum_r)) {
            MBJS_FreeValue(ctx, MBJS_GetException(ctx));
            printf("FAIL: jit sum threw\n");
            return 1;
        }

        MBJS_ToInt64(ctx, &v, sum_r);
        MBJS_FreeValue(ctx, sum_r);
        MBJS_FreeValue(ctx, fib_r);
        total += v + 121393; /* 121393 = fib(26), accumulated in C */
    }
    t1 = now_ms();

    printf("JIT  : fib(%d) + sumTo(%d) x %d = %lld  (%s)\n",
           FIB_N, SUM_N, ITERS, (long long)total,
           total == expect ? "OK" : "FAILED");
    if (total != expect)
        return 1;

    stats = mbjs_jit_stats(jit);
    printf("JIT  : %.1f ms total (compile=once, dispatch=cached)\n", t1 - t0);
    printf("JIT  : stats compile_ok=%lu cache_hits=%lu native_calls=%lu "
           "interp_calls=%lu\n",
           stats.compile_ok, stats.cache_hits, stats.native_calls,
           stats.interp_calls);
    if (stats.compile_ok < 1) {
        printf("FAIL: nothing compiled\n");
        return 1;
    }
    /* fib+sum called ITERS times each; every call after the first two
     * compilations must be a cache hit. */
    n = (int)stats.cache_hits;
    if (n != 2 * ITERS - 2) {
        printf("FAIL: expected %d cache hits, got %d\n", 2 * ITERS - 2, n);
        return 1;
    }
    if (stats.native_calls != 2 * ITERS) {
        printf("FAIL: expected %d native calls, got %lu\n", 2 * ITERS,
               stats.native_calls);
        return 1;
    }

    mbjs_jit_cleanup_all(jit, ctx);
    mbjs_jit_free(jit);

    /* --- Phase 2: second boot with the persistent cache ------------------- */

    {
#if !defined(_WIN32)
        char rm_cmd[512];
#endif
        MBJSValue fib_r, sum_r;
        int64_t v;
        double t2;

        /* Same opts.cache_dir as phase 1: the shims compiled there are now
         * on disk, so the second boot must be a pure dlopen fast path. */
        jit = mbjs_jit_new(MBJS_JIT_BACKEND_CC, &opts);
        if (jit == NULL || strcmp(mbjs_jit_backend_name(jit), "cc") != 0) {
            printf("FAIL: second-boot JIT unavailable\n");
            return 1;
        }

        t2 = now_ms();
        argv[0] = MBJS_NewInt32(ctx, FIB_N);
        fib_r = mbjs_jit_call(jit, ctx, FIB_SRC, "fib", MBJS_UNDEFINED, 1, argv);
        MBJS_FreeValue(ctx, argv[0]);
        if (MBJS_IsException(fib_r)) {
            MBJS_FreeValue(ctx, MBJS_GetException(ctx));
            printf("FAIL: second-boot fib threw\n");
            return 1;
        }
        argv[0] = MBJS_NewInt32(ctx, SUM_N);
        sum_r = mbjs_jit_call(jit, ctx, SUM_SRC, "sumTo", MBJS_UNDEFINED, 1, argv);
        MBJS_FreeValue(ctx, argv[0]);
        if (MBJS_IsException(sum_r)) {
            MBJS_FreeValue(ctx, MBJS_GetException(ctx));
            printf("FAIL: second-boot sum threw\n");
            return 1;
        }
        MBJS_ToInt64(ctx, &v, sum_r);
        MBJS_FreeValue(ctx, sum_r);
        MBJS_FreeValue(ctx, fib_r);
        t2 = now_ms() - t2;

        stats = mbjs_jit_stats(jit);
        printf("JIT2 : %.2f ms for the same work (cc skipped entirely)\n", t2);
        printf("JIT2 : stats cache_disk_hits=%lu compile_attempts=%lu "
               "cache_disk_writes=%lu\n",
               stats.cache_disk_hits, stats.compile_attempts,
               stats.cache_disk_writes);
        if (stats.cache_disk_hits != 2) {
            printf("FAIL: expected 2 disk hits, got %lu\n",
                   stats.cache_disk_hits);
            return 1;
        }
        if (stats.compile_attempts != 0) {
            printf("FAIL: second boot invoked the compiler %lu times\n",
                   stats.compile_attempts);
            return 1;
        }
        if (v != 5000050000LL) {
            printf("FAIL: second-boot sum wrong: %lld\n", (long long)v);
            return 1;
        }

        mbjs_jit_cleanup_all(jit, ctx);
        mbjs_jit_free(jit);
#if defined(_WIN32)
        {
            /* Best effort: remove the scratch tree left by the JIT. */
            char rm_cmd[1024];
            snprintf(rm_cmd, sizeof(rm_cmd),
                     "rmdir /s /q \"%s\" >nul 2>&1", opts.cache_dir);
            if (system(rm_cmd) != 0) {
                /* leftover temp dir is harmless */
            }
        }
#else
        snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", opts.cache_dir);
        if (system(rm_cmd) != 0) {
            /* best effort: a leftover temp dir is harmless */
        }
#endif
    }

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
    printf("bench-jit: ALL OK\n");
    return 0;
}
