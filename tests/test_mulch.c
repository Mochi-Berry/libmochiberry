/*
 * test_mulch.c — tests for libmulchberry (mbjs-mulch), the four-mode memory
 * manager.
 *
 * Exercises each mode:
 *   MANUAL      alloc/free/reuse
 *   MEMORY      freed blocks are zeroed and recycled; compost drops empty beds
 *   SEMI_AUTO   scope reclamation, nesting, explicit free still honoured
 *   AUTO        generational mark-sweep (young promotion, full sweep)
 * plus capacity stats, the audit and running the engine on a manager.
 *
 * Build & run (manual toolchain):
 *   gcc -O2 -g -std=gnu11 -D_GNU_SOURCE -I. -c mbjs-mulch.c -o build_manual/mbjs-mulch.o
 *   gcc -O2 -g -std=gnu11 -D_GNU_SOURCE -I. tests/test_mulch.c build_manual/mbjs-mulch.o \
 *       build_manual/libmbjs.a -lm -lpthread -o build_manual/test-mulch.exe
 *   build_manual/test-mulch.exe
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mbjs.h"
#include "mbjs-mulch.h"

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

/* --- MANUAL ---------------------------------------------------------------- */

static void test_manual(void)
{
    MBJSMulch *m = mbjs_mulch_new(MBJS_MULCH_MANUAL);
    MBJSMulchStats s;
    void *p[200];
    int i, ok = 1;

    CHECK(m != NULL, "manual: manager created");
    CHECK(strcmp(mbjs_mulch_mode_name_of(m), "manual") == 0,
          "manual: mode name");
    CHECK(mbjs_mulch_mode(m) == MBJS_MULCH_MANUAL, "manual: mode value");

    for (i = 0; i < 200; i++) {
        p[i] = mbjs_mulch_alloc(m, (size_t)(i + 1));
        if (!p[i])
            ok = 0;
        if (((uintptr_t)p[i] % MBJS_MULCH_ALIGN) != 0)
            ok = 0;
    }
    CHECK(ok, "manual: 200 allocations are non-NULL and aligned");

    memcpy(p[10], "hello-mulch", 12);
    CHECK(memcmp(p[10], "hello-mulch", 12) == 0, "manual: writes persist");

    s = mbjs_mulch_stats(m);
    CHECK(s.live_cells == 200, "manual: live cell count is 200");
    CHECK(s.beds >= 1, "manual: at least one bed in use");
    CHECK(s.peak_live_bytes >= s.live_bytes, "manual: peak >= live bytes");

    {
        size_t recyclable = s.recycled;
        for (i = 0; i < 200; i++)
            mbjs_mulch_free(m, p[i]);
        s = mbjs_mulch_stats(m);
        CHECK(s.live_cells == 0, "manual: freeing all leaves 0 live cells");
        CHECK(s.free_calls == 200, "manual: freed count is 200");

        for (i = 0; i < 200; i++)
            p[i] = mbjs_mulch_alloc(m, 16);
        s = mbjs_mulch_stats(m);
        CHECK(s.recycled > recyclable, "manual: blocks were recycled");
    }

    for (i = 0; i < 200; i++)
        mbjs_mulch_free(m, p[i]);

    CHECK(mbjs_mulch_usable_size(NULL) == 0, "manual: usable_size(NULL) = 0");
    CHECK(mbjs_mulch_audit(m) == 0, "manual: audit clean");
    mbjs_mulch_destroy(m);
}

/* --- MEMORY ---------------------------------------------------------------- */

static void test_memory(void)
{
    MBJSMulch *m = mbjs_mulch_new(MBJS_MULCH_MEMORY);
    MBJSMulchStats s;
    unsigned char *a, *b;
    int i, all_zero = 1;

    CHECK(m != NULL, "memory: manager created");
    a = (unsigned char *)mbjs_mulch_alloc(m, 128);
    CHECK(a != NULL, "memory: alloc returns a block");
    memset(a, 0xab, 128);
    mbjs_mulch_free(m, a);

    for (i = 0; i < 128; i++)
        if (a[i] != 0)
            all_zero = 0;
    CHECK(all_zero, "memory: freed block is zeroed (mulched)");

    b = (unsigned char *)mbjs_mulch_alloc(m, 128);
    CHECK(b == a, "memory: same-size alloc reuses the mulched block");
    all_zero = 1;
    for (i = 0; i < 128; i++)
        if (b[i] != 0)
            all_zero = 0;
    CHECK(all_zero, "memory: reused block is clean");

    mbjs_mulch_free(m, b);

    memset(mbjs_mulch_alloc(m, 4000), 1, 4000);
    {
        void *big = mbjs_mulch_alloc(m, 100000);
        CHECK(big != NULL, "memory: large allocation works");
        mbjs_mulch_free(m, big);
    }

    mbjs_mulch_compost(m);
    s = mbjs_mulch_stats(m);
    CHECK(s.live_cells == 1, "memory: compost keeps live blocks");
    CHECK(s.reserved_bytes < 100000, "memory: compost released the large bed");

    mbjs_mulch_destroy(m);
}

/* --- SEMI_AUTO ------------------------------------------------------------- */

static void test_semi_auto(void)
{
    MBJSMulch *m = mbjs_mulch_new(MBJS_MULCH_SEMI_AUTO);
    MBJSMulchStats s;
    uint64_t outer, inner;
    void *keep, *o1, *i1, *i2;

    CHECK(m != NULL, "semi: manager created");
    keep = mbjs_mulch_alloc(m, 32);
    CHECK(keep != NULL, "semi: allocation outside any scope persists");

    outer = mbjs_mulch_scope_begin(m);
    CHECK(outer != 0, "semi: outer scope opened");
    o1 = mbjs_mulch_alloc(m, 64);

    inner = mbjs_mulch_scope_begin(m);
    CHECK(inner > outer, "semi: inner scope id is newer");
    i1 = mbjs_mulch_alloc(m, 64);
    i2 = mbjs_mulch_alloc(m, 64);
    CHECK(mbjs_mulch_scope_current(m) == inner, "semi: current scope is inner");

    mbjs_mulch_free(m, i1); /* explicit free still honoured */

    mbjs_mulch_scope_end(m, inner);
    s = mbjs_mulch_stats(m);
    CHECK(s.live_cells == 2, "semi: inner end reclaimed i2, kept o1+keep");

    mbjs_mulch_scope_end(m, outer);
    s = mbjs_mulch_stats(m);
    CHECK(s.live_cells == 1, "semi: outer end reclaimed o1, kept keep");
    CHECK(s.collected >= 2, "semi: scope reclamation counted as collected");

    mbjs_mulch_free(m, keep);
    s = mbjs_mulch_stats(m);
    CHECK(s.live_cells == 0, "semi: explicit free of outer allocation works");

    {
        MBJSMulch *mm = mbjs_mulch_new(MBJS_MULCH_MANUAL);
        CHECK(mbjs_mulch_scope_begin(mm) == 0,
              "semi: scopes are a no-op in MANUAL");
        mbjs_mulch_destroy(mm);
    }

    mbjs_mulch_destroy(m);
}

/* --- AUTO ------------------------------------------------------------------ */

static void test_auto(void)
{
    MBJSMulch *m = mbjs_mulch_new(MBJS_MULCH_AUTO);
    MBJSMulchStats s;
    void *p[100];
    int i;

    CHECK(m != NULL, "auto: manager created");
    mbjs_mulch_set_threshold(m, 0); /* deterministic: explicit collect only */
    for (i = 0; i < 100; i++)
        p[i] = mbjs_mulch_alloc(m, 24);

    for (i = 0; i < 10; i++)
        mbjs_mulch_mark(m, p[i]);

    mbjs_mulch_collect(m);
    s = mbjs_mulch_stats(m);
    CHECK(s.live_cells == 10, "auto: young sweep freed the 90 unmarked");
    CHECK(s.collected == 90, "auto: 90 blocks collected");

    /* The 10 survivors are now old. Allocate a fresh unmarked young block and
     * run a *young* collect: the old survivors must remain. */
    {
        void *young = mbjs_mulch_alloc(m, 24);
        (void)young;
        mbjs_mulch_collect(m);
        s = mbjs_mulch_stats(m);
        CHECK(s.live_cells == 10, "auto: old survivors survive a young collect");
    }

    /* Mark five of the old generation, then a full sweep frees the unmarked
     * old blocks. */
    for (i = 0; i < 5; i++)
        mbjs_mulch_mark(m, p[i]);
    mbjs_mulch_collect_full(m);
    s = mbjs_mulch_stats(m);
    CHECK(s.live_cells == 5, "auto: full sweep keeps only marked old blocks");

    /* Automatic trigger: mark everything, set a tiny budget, then allocate;
     * the trigger must collect (promoting marked blocks) without freeing the
     * block it is about to return. */
    for (i = 0; i < 5; i++)
        mbjs_mulch_mark(m, p[i]);
    mbjs_mulch_set_threshold(m, 1);
    {
        void *q = mbjs_mulch_alloc(m, 24);
        CHECK(q != NULL, "auto: allocation succeeds after auto-collect");
        s = mbjs_mulch_stats(m);
        CHECK(s.live_cells == 6, "auto: fresh block survives its own trigger");
    }

    mbjs_mulch_destroy(m);
}

/* --- engine on a manager --------------------------------------------------- */

static void test_engine(void)
{
    MBJSMulch *m = mbjs_mulch_new(MBJS_MULCH_MANUAL);
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSValue v;
    int32_t n = 0;
    MBJSMulchStats s;

    CHECK(m != NULL, "engine: manager created");
    rt = MBJS_NewRuntime2(mbjs_mulch_mf(), m);
    CHECK(rt != NULL, "engine: runtime created on mulch allocator");
    if (!rt) {
        mbjs_mulch_destroy(m);
        return;
    }
    ctx = MBJS_NewContext(rt);
    CHECK(ctx != NULL, "engine: context created");
    v = MBJS_Eval(ctx, "1 + 2 * 3", 9, "test_mulch.js", MBJS_EVAL_TYPE_GLOBAL);
    CHECK(!MBJS_IsException(v), "engine: eval succeeds");
    if (!MBJS_IsException(v)) {
        MBJS_ToInt32(ctx, &n, v);
        CHECK(n == 7, "engine: eval result is 7");
    }
    MBJS_FreeValue(ctx, v);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);

    s = mbjs_mulch_stats(m);
    CHECK(s.alloc_calls > 0, "engine: allocations flowed through libmulchberry");
    CHECK(mbjs_mulch_audit(m) == 0, "engine: audit clean after runtime teardown");
    mbjs_mulch_destroy(m);
}

int main(void)
{
    printf("== libmulchberry (mbjs-mulch) tests ==\n");
    test_manual();
    test_memory();
    test_semi_auto();
    test_auto();
    test_engine();

    if (g_failures == 0) {
        printf("== ALL MULCH TESTS PASSED ==\n");
        return 0;
    }
    printf("== %d MULCH TEST(S) FAILED ==\n", g_failures);
    return 1;
}
