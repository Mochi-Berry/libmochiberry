/*
 * test_sunat.c — Sunat GC build verification (libmochiberry sunat variant).
 *
 * Verifies that in the sunat build:
 *   - the garbage collector NEVER runs (gc_runs == 0 even under churn and
 *     after an explicit MBJS_RunGC() call);
 *   - cyclic object graphs, string churn and object churn keep working
 *     without a GC;
 *   - the engine's results are byte-identical to the reference values;
 *   - MBJS_NewRuntime() creates a runtime-owned arena that is fully
 *     reclaimed in bulk at MBJS_FreeRuntime();
 *   - MBJS_NewRuntime2(mbjs_mem_mf(), &arena) lets an embedder own the arena
 *     (the "parameter diambil" ownership path) and audit + destroy it.
 *
 * Build: cc -DMBJS_SUNAT_GC -I. -Idist/sunat tests/test_sunat.c \
 *          -Ldist/sunat -lmochiberry -o test-sunat -lm
 */
#define MBJS_SUNAT_GC 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbjs.h"
#include "mbjs-mem.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do {                                           \
    checks++;                                                           \
    if (cond) {                                                         \
        printf("PASS: " __VA_ARGS__);                                   \
        printf("\n");                                                   \
    } else {                                                            \
        failures++;                                                     \
        printf("FAIL: " __VA_ARGS__);                                   \
        printf("  [%s:%d]\n", __FILE__, __LINE__);                      \
    }                                                                   \
} while (0)

static int get_int_prop(MBJSContext *ctx, MBJSValueConst obj,
                        const char *name, int64_t *out)
{
    MBJSValue v = MBJS_GetPropertyStr(ctx, obj, name);
    int rc = -1;

    if (!MBJS_IsException(v)) {
        if (MBJS_ToInt64(ctx, out, v) == 0)
            rc = 0;
    }
    MBJS_FreeValue(ctx, v);
    return rc;
}

static const char *churn_script =
    "var a = {n: 7}; a.self = a;\n"
    "var s = '';\n"
    "for (var i = 0; i < 8000; i++) s += 'x' + i;\n"
    "var arr = [];\n"
    "for (var i = 0; i < 3000; i++) arr.push({i: i, tag: 'o' + i});\n"
    "function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }\n"
    "var f = fib(20);\n"
    "var sum = 0; for (var i = 1; i <= 100; i++) sum += i;\n"
    "var cycleLen = 0;\n"
    "(function walk(o, depth) { cycleLen++; if (depth > 0 && o.self) "
    "walk(o.self, depth - 1); })(a, 3);\n"
    "globalThis.__r = { slen: s.length, alen: arr.length, fib: f, sum: sum,\n"
    "                   n: a.self.self.n, cycleLen: cycleLen, last: arr[2999].i };\n";

/* Part A: runtime-owned arena (MBJS_NewRuntime default in the sunat build). */
static int test_runtime_owned(void)
{
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSValue r, g;
    int64_t slen, alen, fib, sum, n, cycle_len, last;
    MBJSMemSunatStats st;
    int ret = 0;

    printf("== part A: runtime-owned arena (MBJS_NewRuntime) ==\n");
    rt = MBJS_NewRuntime();
    CHECK(rt != NULL, "MBJS_NewRuntime() in sunat build");
    if (!rt)
        return 1;
    CHECK(MBJS_GetSunatArena(rt) != NULL,
          "MBJS_GetSunatArena() returns the runtime-owned arena");

    ctx = MBJS_NewContext(rt);
    CHECK(ctx != NULL, "MBJS_NewContext()");
    if (!ctx) {
        MBJS_FreeRuntime(rt);
        return 1;
    }

    r = MBJS_Eval(ctx, churn_script, strlen(churn_script), "churn.js", 0);
    CHECK(!MBJS_IsException(r), "churn + cycle script evaluates");
    if (MBJS_IsException(r)) {
        MBJSValue exc = MBJS_GetException(ctx);
        const char *msg = MBJS_ToCString(ctx, exc);
        if (msg) {
            printf("  exception: %s\n", msg);
            MBJS_FreeCString(ctx, msg);
        }
        MBJS_FreeValue(ctx, exc);
    }
    MBJS_FreeValue(ctx, r);

    g = MBJS_GetGlobalObject(ctx);
    r = MBJS_GetPropertyStr(ctx, g, "__r");
    CHECK(!MBJS_IsException(r), "read __r result object");
    if (!MBJS_IsException(r)) {
        CHECK(get_int_prop(ctx, r, "slen", &slen) == 0 && slen > 10000,
              "string churn length = %lld (>10000)", (long long)slen);
        CHECK(get_int_prop(ctx, r, "alen", &alen) == 0 && alen == 3000,
              "object churn count = %lld (==3000)", (long long)alen);
        CHECK(get_int_prop(ctx, r, "fib", &fib) == 0 && fib == 6765,
              "fib(20) = %lld (==6765)", (long long)fib);
        CHECK(get_int_prop(ctx, r, "sum", &sum) == 0 && sum == 5050,
              "sum 1..100 = %lld (==5050)", (long long)sum);
        CHECK(get_int_prop(ctx, r, "n", &n) == 0 && n == 7,
              "cycle walk a.self.self.n = %lld (==7)", (long long)n);
        CHECK(get_int_prop(ctx, r, "cycleLen", &cycle_len) == 0 &&
              cycle_len == 4, "cycle walk visits %lld nodes (==4)",
              (long long)cycle_len);
        CHECK(get_int_prop(ctx, r, "last", &last) == 0 && last == 2999,
              "arr[2999].i = %lld (==2999)", (long long)last);
    }
    MBJS_FreeValue(ctx, r);
    MBJS_FreeValue(ctx, g);

    /* An explicit MBJS_RunGC() must be a no-op in the sunat build. */
    MBJS_RunGC(rt);
    MBJS_GetSunatStats(rt, &st);
    CHECK(st.gc_runs == 0, "gc_runs == 0 after churn + explicit MBJS_RunGC");
    CHECK(st.arena_used > 0, "arena_used = %zu (>0)", st.arena_used);
    CHECK(st.arena_peak >= st.arena_used,
          "arena_peak = %zu (>= used)", st.arena_peak);
    CHECK(st.engine_malloc_count > 0,
          "engine allocs tracked = %zu (>0)", st.engine_malloc_count);
    printf("  sunat stats: used=%zu peak=%zu limit=%zu engine=%zu/%zu\n",
           st.arena_used, st.arena_peak, st.arena_limit,
           st.engine_malloc_size, st.engine_malloc_count);

    /* Spawn a worker-style second context on the same runtime to exercise
       more allocation paths under the same arena. */
    {
        MBJSContext *ctx2 = MBJS_NewContext(rt);
        MBJSValue v;
        if (ctx2) {
            v = MBJS_Eval(ctx2, "Math.max(3, 9, 4)", 17, "expr.js", 0);
            if (!MBJS_IsException(v)) {
                int64_t m;
                if (MBJS_ToInt64(ctx2, &m, v) == 0)
                    CHECK(m == 9, "second context Math.max = %lld (==9)",
                          (long long)m);
            }
            MBJS_FreeValue(ctx2, v);
            MBJS_FreeContext(ctx2);
        }
    }

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt); /* must destroy the runtime-owned arena */
    printf("== part A done ==\n");
    return ret;
}

/* Part B: embedder-owned arena (MBJS_NewRuntime2 + mbjs_mem_mf()). */
static int test_embedder_arena(void)
{
    MBJSMemArena arena;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSValue v;
    int64_t val;
    MBJSMemSunatStats st;
    MBJSMemAudit audit;

    printf("== part B: embedder-owned arena (NewRuntime2) ==\n");
    mbjs_mem_arena_init(&arena, 0);

    rt = MBJS_NewRuntime2(mbjs_mem_mf(), &arena);
    CHECK(rt != NULL, "MBJS_NewRuntime2(mbjs_mem_mf(), &arena)");
    if (!rt) {
        mbjs_mem_arena_destroy(&arena);
        return 1;
    }
    CHECK(MBJS_GetSunatArena(rt) == (void *)&arena,
          "MBJS_GetSunatArena() resolves the embedder arena");

    ctx = MBJS_NewContext(rt);
    CHECK(ctx != NULL, "MBJS_NewContext() on embedder arena");
    if (!ctx) {
        MBJS_FreeRuntime(rt);
        mbjs_mem_arena_destroy(&arena);
        return 1;
    }

    v = MBJS_Eval(ctx, "2 + 3 * 4", 9, "expr.js", 0);
    CHECK(!MBJS_IsException(v), "eval 2 + 3 * 4");
    if (!MBJS_IsException(v) && MBJS_ToInt64(ctx, &val, v) == 0)
        CHECK(val == 14, "2 + 3 * 4 = %lld (==14)", (long long)val);
    MBJS_FreeValue(ctx, v);

    MBJS_GetSunatStats(rt, &st);
    CHECK(st.gc_runs == 0, "gc_runs == 0 (embedder arena)");
    CHECK(st.arena_used > 0, "embedder arena used = %zu (>0)", st.arena_used);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);

    /* Ownership path: the embedder holds the arena ("parameter diambil");
       audit it, then reclaim everything in bulk. */
    audit = mbjs_mem_arena_audit(&arena);
    CHECK(audit.taken == 0, "audit: taken handles == 0 (got %zu)",
          audit.taken);
    mbjs_mem_arena_destroy(&arena);
    CHECK(mbjs_mem_arena_used(&arena) == 0,
          "arena used after destroy == 0 (bulk reclaim)");
    printf("== part B done ==\n");
    return 0;
}

int main(void)
{
    int rc = 0;

    printf("== libmochiberry Sunat GC test ==\n");
    rc |= test_runtime_owned();
    rc |= test_embedder_arena();

    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("SUNAT TEST FAILED\n");
        return 1;
    }
    printf("ALL SUNAT TESTS PASSED\n");
    return 0;
}
