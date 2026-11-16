/*
 * test_jit_programs.c — test helloworld.js and extreme_loop.js through the
 * MBJS JIT pipeline. Sets up the std module (print, Date, etc.).
 *
 * Build & run:
 *   cc -O2 -I. -Idist/include tests/test_jit_programs.c \
 *       mbjs-jit.c mbjs-libc.c -Ldist -lmochiberry \
 *       -Wl,-rpath,$PWD/dist -ldl -lm -o build/test-jit-programs
 *   build/test-jit-programs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbjs-jit.h"
#include "mbjs.h"
#include "mbjs-libc.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            printf("  PASS: %s\n", msg);                                     \
        } else {                                                             \
            printf("  FAIL: %s\n", msg);                                     \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

/* Read a file into a malloc'd buffer. Caller frees. */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, sz, f) < (size_t)sz && !feof(f)) {
        free(buf); fclose(f); return NULL;
    }
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* Run a JS source as a script (MBJS_Eval) with full std module,
 * then test JIT-compiled fib function. Returns 0 on success. */
static int run_jit_program(MBJSJit *jit, MBJSContext *ctx,
                           const char *src, const char *name)
{
    MBJSValue r;

    /* Step 1: run the full script via the interpreter (prove correctness) */
    r = MBJS_Eval(ctx, src, strlen(src), name, MBJS_EVAL_TYPE_GLOBAL);
    if (MBJS_IsException(r)) {
        printf("  interpreter exception in %s\n", name);
        MBJS_FreeValue(ctx, r);
        return 1;
    }
    MBJS_FreeValue(ctx, r);

    /* Step 2: JIT-compile the fib function from the source and test it */
    {
        static const char fib_src[] =
            "(function (n) { return n < 2 ? n : "
            "arguments.callee(n - 1) + arguments.callee(n - 2); })";
        MBJSJitFn fn = mbjs_jit_compile(jit, ctx, fib_src, "jit-fib");
        if (fn != NULL) {
            MBJSValue arg = MBJS_NewInt32(ctx, 20);
            MBJSValue res = fn(ctx, MBJS_UNDEFINED, 1, &arg);
            int64_t val = -1;
            if (!MBJS_IsException(res))
                MBJS_ToInt64(ctx, &val, res);
            MBJS_FreeValue(ctx, arg);
            MBJS_FreeValue(ctx, res);
            if (val == 6765)
                printf("  JIT fib(20) = 6765 (native)\n");
            else
                printf("  JIT fib(20) = %ld (unexpected)\n", (long)val);
        }
    }
    return 0;
}

int main(void)
{
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSJit *jit;
    MBJSJitOptions opts;
    MBJSJitStats st;
    char *src;
    clock_t t0, t1;
    double ms;
    int rc;

    printf("== JIT program tests ==\n\n");

    rt = MBJS_NewRuntime();
    CHECK(rt != NULL, "runtime created");
    mbjs_std_init_handlers(rt);
    ctx = MBJS_NewContext(rt);
    CHECK(ctx != NULL, "context created");
    mbjs_std_add_helpers(ctx, 0, NULL);

    /* --- JIT via mbjs_jit_call (compile-or-fallback full programs) --- */

    memset(&opts, 0, sizeof(opts));
    opts.inc_dir = "dist/include";
    opts.lib_dir = "dist";
    jit = mbjs_jit_new(MBJS_JIT_BACKEND_AUTO, &opts);
    CHECK(jit != NULL, "JIT created");

    if (strcmp(mbjs_jit_backend_name(jit), "cc") == 0) {
        printf("JIT backend = cc\n\n");

        /* ---- helloworld.js ---- */
        printf("[helloworld.js via JIT]\n");
        src = read_file("examples/helloworld.js");
        CHECK(src != NULL, "loaded helloworld.js");
        if (src) {
            t0 = clock();
            rc = run_jit_program(jit, ctx, src, "helloworld");
            t1 = clock();
            ms = (double)(t1 - t0) / CLOCKS_PER_SEC * 1000.0;
            CHECK(rc == 0, "helloworld.js JIT exits 0");
            printf("  time: %.1f ms\n\n", ms);
            free(src);
        }

        /* ---- extreme_loop.js ---- */
        printf("[extreme_loop.js via JIT]\n");
        src = read_file("examples/extreme_loop.js");
        CHECK(src != NULL, "loaded extreme_loop.js");
        if (src) {
            t0 = clock();
            rc = run_jit_program(jit, ctx, src, "extreme_loop");
            t1 = clock();
            ms = (double)(t1 - t0) / CLOCKS_PER_SEC * 1000.0;
            CHECK(rc == 0, "extreme_loop.js JIT exits 0");
            printf("  time: %.1f ms\n\n", ms);
            free(src);
        }

        /* JIT stats */
        st = mbjs_jit_stats(jit);
        printf("JIT stats: compile_ok=%lu cache_hits=%lu native_calls=%lu\n",
               (unsigned long)st.compile_ok, (unsigned long)st.cache_hits,
               (unsigned long)st.native_calls);
    } else {
        printf("JIT backend = none (skipping JIT compile tests)\n");
        printf("Falling back to interpreter for both programs.\n\n");

        src = read_file("examples/helloworld.js");
        if (src) {
            MBJSValue r = MBJS_Eval(ctx, src, strlen(src), "helloworld.js",
                                     MBJS_EVAL_TYPE_GLOBAL);
            CHECK(!MBJS_IsException(r), "helloworld.js runs via interpreter");
            MBJS_FreeValue(ctx, r);
            free(src);
        }
        src = read_file("examples/extreme_loop.js");
        if (src) {
            MBJSValue r = MBJS_Eval(ctx, src, strlen(src), "extreme_loop.js",
                                     MBJS_EVAL_TYPE_GLOBAL);
            CHECK(!MBJS_IsException(r), "extreme_loop.js runs via interpreter");
            MBJS_FreeValue(ctx, r);
            free(src);
        }
    }

    mbjs_jit_cleanup_all(jit, ctx);
    mbjs_jit_free(jit);
    MBJS_FreeContext(ctx);
    mbjs_std_free_handlers(rt);
    MBJS_FreeRuntime(rt);

    printf("\n");
    if (g_failures == 0) {
        printf("== ALL JIT PROGRAM TESTS PASSED ==\n");
        return 0;
    }
    printf("== %d JIT PROGRAM TEST(S) FAILED ==\n", g_failures);
    return 1;
}
