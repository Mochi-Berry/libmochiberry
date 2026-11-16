/*
 * test_jit.c — tests for the JIT foundation (mbjs-jit).
 *
 * Exercises the runtime-C-compilation backend ("cc"), the interpreter
 * fallback path, the hot-function tracker and the "none" backend used on
 * bare-metal / custom OSes.
 *
 * Build & run:
 *   cmake --build build --target test-jit && build/test-jit
 *   (or: make jittest)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbjs-jit.h"
#include "mbjs.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            printf("PASS: %s\n", msg);                                       \
        } else {                                                             \
            printf("FAIL: %s\n", msg);                                       \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

/* fib(10) == 55. Self-contained (no recursion through globals) so the same
 * source string works in the interpreter and in the JIT shim. */
static const char FIB_SRC[] =
    "(function (n) { return n < 2 ? n : "
    "arguments.callee(n - 1) + arguments.callee(n - 2); })";

static int call_and_get_int(MBJSContext *ctx, MBJSJitFn fn, MBJSValue this_val,
                            int32_t arg)
{
    MBJSValue argv[1];
    MBJSValue r;
    int32_t n = -1;

    argv[0] = MBJS_NewInt32(ctx, arg);
    r = fn(ctx, this_val, 1, argv);
    MBJS_FreeValue(ctx, argv[0]);
    if (MBJS_IsException(r)) {
        MBJS_FreeValue(ctx, MBJS_GetException(ctx));
        return -1;
    }
    MBJS_ToInt32(ctx, &n, r);
    MBJS_FreeValue(ctx, r);
    return n;
}

int main(void)
{
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSJit *jit;
    MBJSJitOptions opts;
    MBJSJitFn fn;
    MBJSJitStats st;
    MBJSValue fref, rref, argv[1];
    int32_t expect = -1, got;
    int i;

    printf("== mbjs-jit tests ==\n");

    rt = MBJS_NewRuntime();
    CHECK(rt != NULL, "runtime created");
    if (rt == NULL)
        return 1;
    ctx = MBJS_NewContext(rt);
    CHECK(ctx != NULL, "context created");

    /* Interpreter reference result for fib(10). */
    fref = MBJS_Eval(ctx, FIB_SRC, strlen(FIB_SRC), "fib-ref",
                     MBJS_EVAL_TYPE_GLOBAL);
    CHECK(!MBJS_IsException(fref), "interpreter evaluates the fib source");
    if (!MBJS_IsException(fref)) {
        argv[0] = MBJS_NewInt32(ctx, 10);
        rref = MBJS_Call(ctx, fref, MBJS_UNDEFINED, 1, argv);
        MBJS_FreeValue(ctx, argv[0]);
        if (!MBJS_IsException(rref)) {
            MBJS_ToInt32(ctx, &expect, rref);
            MBJS_FreeValue(ctx, rref);
        }
        MBJS_FreeValue(ctx, fref);
    }
    CHECK(expect == 55, "interpreter fib(10) == 55");

    /* --- AUTO backend ----------------------------------------------------- */

    memset(&opts, 0, sizeof(opts));
    /* NULL inc_dir/lib_dir -> auto-defaults (host exe dir on win32). */
    opts.hot_threshold = 3;
    jit = mbjs_jit_new(MBJS_JIT_BACKEND_AUTO, &opts);
    CHECK(jit != NULL, "JIT created (AUTO)");

    if (strcmp(mbjs_jit_backend_name(jit), "cc") == 0) {
        printf("note: JIT backend = cc (runtime C compilation)\n");

        fn = mbjs_jit_compile(jit, ctx, FIB_SRC, "fib");
        CHECK(fn != NULL, "mbjs_jit_compile produces a native function");
        if (fn != NULL) {
            got = call_and_get_int(ctx, fn, MBJS_UNDEFINED, 10);
            CHECK(got == 55, "JIT-compiled fib(10) == 55");
            got = call_and_get_int(ctx, fn, MBJS_UNDEFINED, 20);
            CHECK(got == 6765, "JIT-compiled fib(20) == 6765");
        }

        /* Compile-or-fallback call path. */
        argv[0] = MBJS_NewInt32(ctx, 10);
        rref = mbjs_jit_call(jit, ctx, FIB_SRC, "fib-call",
                             MBJS_UNDEFINED, 1, argv);
        MBJS_FreeValue(ctx, argv[0]);
        got = -1;
        if (!MBJS_IsException(rref)) {
            MBJS_ToInt32(ctx, &got, rref);
            MBJS_FreeValue(ctx, rref);
        }
        CHECK(got == 55, "mbjs_jit_call returns the correct result");

        /* Hot-function tracker: threshold 3, six observe() calls. */
        for (i = 0; i < 6; i++) {
            MBJSJitFn hf = mbjs_jit_observe(jit, ctx, FIB_SRC, "hotfib");
            if (hf != NULL) {
                got = call_and_get_int(ctx, hf, MBJS_UNDEFINED, 10);
                CHECK(got == 55, "hot function ran natively with correct result");
                break;
            }
        }
        st = mbjs_jit_stats(jit);
        CHECK(st.hot_tracked >= 1, "hot tracker registered the function");
        CHECK(st.compile_ok >= 1, "at least one successful compilation");
        CHECK(st.native_calls >= 1, "native calls were dispatched");
    } else {
        printf("note: JIT backend = none (no toolchain in this environment)\n");
        fn = mbjs_jit_compile(jit, ctx, FIB_SRC, "fib");
        CHECK(fn == NULL, "compile fails cleanly with backend \"none\"");
        argv[0] = MBJS_NewInt32(ctx, 10);
        rref = mbjs_jit_call(jit, ctx, FIB_SRC, "fib-call",
                             MBJS_UNDEFINED, 1, argv);
        MBJS_FreeValue(ctx, argv[0]);
        got = -1;
        if (!MBJS_IsException(rref)) {
            MBJS_ToInt32(ctx, &got, rref);
            MBJS_FreeValue(ctx, rref);
        }
        CHECK(got == 55, "mbjs_jit_call falls back to the interpreter");
    }
    mbjs_jit_cleanup_all(jit, ctx);
    mbjs_jit_free(jit);

    /* --- Persistent compilation cache (.mbjso) ------------------------------ */

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    {
        char cache_dir[] = "/tmp/mbjsjit-cache-XXXXXX";
        char rm_cmd[512];

        /* JIT #1: compile with the persistent cache enabled. */
        memset(&opts, 0, sizeof(opts));
        opts.cache_dir = mkdtemp(cache_dir);
        CHECK(opts.cache_dir != NULL, "persistent cache dir created");
        if (opts.cache_dir != NULL) {
            jit = mbjs_jit_new(MBJS_JIT_BACKEND_CC, &opts);
            CHECK(jit != NULL, "JIT created with cache_dir");
            if (strcmp(mbjs_jit_backend_name(jit), "cc") == 0) {
                fn = mbjs_jit_compile(jit, ctx, FIB_SRC, "fib-cached");
                CHECK(fn != NULL, "first boot compiles the function");
                if (fn != NULL) {
                    got = call_and_get_int(ctx, fn, MBJS_UNDEFINED, 10);
                    CHECK(got == 55, "first-boot cached function returns 55");
                }
                st = mbjs_jit_stats(jit);
                CHECK(st.cache_disk_misses >= 1,
                      "first boot counts a disk-cache miss");
                CHECK(st.cache_disk_writes >= 1,
                      "first boot persists the compiled .so");
                mbjs_jit_cleanup_all(jit, ctx);
                mbjs_jit_free(jit);

                /* JIT #2 (a fresh process in the field): same cache dir,
                 * same source — must dlopen the cached .so and skip cc. */
                jit = mbjs_jit_new(MBJS_JIT_BACKEND_CC, &opts);
                CHECK(jit != NULL, "JIT recreated with same cache_dir");
                fn = mbjs_jit_compile(jit, ctx, FIB_SRC, "fib-cached");
                CHECK(fn != NULL,
                      "second boot loads from the persistent cache");
                if (fn != NULL) {
                    got = call_and_get_int(ctx, fn, MBJS_UNDEFINED, 10);
                    CHECK(got == 55, "cached function still returns 55");
                    got = call_and_get_int(ctx, fn, MBJS_UNDEFINED, 20);
                    CHECK(got == 6765, "cached function still returns 6765");
                }
                st = mbjs_jit_stats(jit);
                CHECK(st.cache_disk_hits >= 1,
                      "second boot hits the disk cache");
                CHECK(st.compile_attempts == 0,
                      "second boot never invoked the compiler");
            } else {
                printf("note: persistent-cache test skipped "
                       "(no cc backend)\n");
            }
            mbjs_jit_cleanup_all(jit, ctx);
            mbjs_jit_free(jit);
            jit = NULL;

            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", cache_dir);
            if (system(rm_cmd) != 0) {
                /* best effort: a leftover temp dir is harmless */
            }
        }
    }
#endif

    /* --- NONE backend (bare-metal / custom OS behaviour) ------------------- */

    jit = mbjs_jit_new(MBJS_JIT_BACKEND_NONE, NULL);
    CHECK(jit != NULL, "JIT created (NONE)");
    CHECK(strcmp(mbjs_jit_backend_name(jit), "none") == 0,
          "NONE backend reports \"none\"");
    CHECK(mbjs_jit_compile(jit, ctx, FIB_SRC, "fib-none") == NULL,
          "NONE backend refuses to compile");
    st = mbjs_jit_stats(jit);
    CHECK(st.compile_failed >= 1, "refused compiles are counted as failed");

    argv[0] = MBJS_NewInt32(ctx, 10);
    rref = mbjs_jit_call(jit, ctx, FIB_SRC, "fib-none-call",
                         MBJS_UNDEFINED, 1, argv);
    MBJS_FreeValue(ctx, argv[0]);
    got = -1;
    if (!MBJS_IsException(rref)) {
        MBJS_ToInt32(ctx, &got, rref);
        MBJS_FreeValue(ctx, rref);
    }
    CHECK(got == 55, "NONE backend still runs code via the interpreter");
    st = mbjs_jit_stats(jit);
    CHECK(st.interp_calls >= 1, "fallback calls counted as interpreter calls");
    mbjs_jit_cleanup_all(jit, ctx);
    mbjs_jit_free(jit);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);

    if (g_failures == 0) {
        printf("== ALL JIT TESTS PASSED ==\n");
        return 0;
    }
    printf("== %d JIT TEST(S) FAILED ==\n", g_failures);
    return 1;
}
