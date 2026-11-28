/*
 * test_mem.c — tests for the non-GC memory management foundation (mbjs-mem).
 *
 * Exercises the bump arena, the ownership-marked handle state machine
 * (init / borrow / take / release / abandon), the live-handle audit, and a
 * full MBJS runtime whose allocator is backed by the arena.
 *
 * Build & run:
 *   cmake --build build --target test-mem && build/test-mem
 *   (or: make memtest)
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mbjs-mem.h"
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

/* --- arena basics ---------------------------------------------------------- */

static void test_arena_basic(void)
{
    MBJSMemArena a;
    void *p, *z, *r;
    size_t used_before;

    mbjs_mem_arena_init(&a, 0);
    CHECK(a.magic == MBJS_MEM_ARENA_MAGIC, "arena initialized");

    p = mbjs_mem_arena_alloc(&a, 100);
    CHECK(p != NULL, "arena alloc returns memory");
    CHECK(((uintptr_t)p % MBJS_MEM_ALIGN) == 0, "arena alloc is aligned");

    z = mbjs_mem_arena_allocz(&a, 64);
    CHECK(z != NULL, "arena allocz returns memory");
    {
        unsigned char *q = (unsigned char *)z;
        int i, zeroed = 1;
        for (i = 0; i < 64; i++)
            if (q[i] != 0)
                zeroed = 0;
        CHECK(zeroed, "arena allocz zero-fills");
    }

    used_before = mbjs_mem_arena_used(&a);
    r = mbjs_mem_arena_realloc(&a, p, 300);
    CHECK(r != NULL, "arena realloc grows");
    CHECK(mbjs_mem_arena_used(&a) >= used_before, "arena realloc tracks usage");
    CHECK(mbjs_mem_arena_peak(&a) >= mbjs_mem_arena_used(&a),
          "arena peak is a high-water mark");

    mbjs_mem_arena_free(&a, z);
    CHECK(mbjs_mem_arena_used(&a) > 0, "arena free is a no-op until reset");

    mbjs_mem_arena_reset(&a);
    CHECK(mbjs_mem_arena_used(&a) == 0, "arena reset releases everything");

    p = mbjs_mem_arena_alloc(&a, 8);
    CHECK(p != NULL, "arena reusable after reset");
    mbjs_mem_arena_destroy(&a);
    CHECK(a.magic != MBJS_MEM_ARENA_MAGIC, "arena destroyed");
}

/* --- memory limit ---------------------------------------------------------- */

static void test_arena_limit(void)
{
    MBJSMemArena a;
    void *p;

    mbjs_mem_arena_init(&a, 64);
    mbjs_mem_arena_set_limit(&a, 1000);

    p = mbjs_mem_arena_alloc(&a, 800);
    CHECK(p != NULL, "allocation within the limit succeeds");

    p = mbjs_mem_arena_alloc(&a, 800);
    CHECK(p == NULL, "allocation beyond the limit fails");

    mbjs_mem_arena_set_limit(&a, 0);
    p = mbjs_mem_arena_alloc(&a, 800);
    CHECK(p != NULL, "removing the limit unblocks allocation");

    mbjs_mem_arena_destroy(&a);
}

/* --- ownership-marked handles ---------------------------------------------- */

static void test_handles(void)
{
    MBJSMemArena a;
    void *p;
    MBJSMemHandle h, b, t;

    mbjs_mem_arena_init(&a, 0);

    p = mbjs_mem_arena_alloc(&a, 64);
    h = mbjs_mem_handle_init(&a, p, 64, "test");
    CHECK(mbjs_mem_handle_valid(h), "handle created");
    CHECK(h.state == MBJS_MEM_TAKEN, "creator owns its handle (TAKEN)");

    b = mbjs_mem_borrow(h, "consumer");
    CHECK(b.state == MBJS_MEM_BORROWED, "borrow marks the handle BORROWED");

    t = mbjs_mem_take(b);
    CHECK(t.state == MBJS_MEM_TAKEN, "take transfers ownership (TAKEN)");

    mbjs_mem_release(&a, &t);
    CHECK(!mbjs_mem_handle_valid(t), "release frees the handle");

    p = mbjs_mem_arena_alloc(&a, 16);
    h = mbjs_mem_handle_init(&a, p, 16, "ephemeral");
    mbjs_mem_abandon(&a, &h);
    CHECK(!mbjs_mem_handle_valid(h), "abandon drops a handle without freeing");

    /* Releasing a borrowed handle is refused: ownership contract holds. */
    p = mbjs_mem_arena_alloc(&a, 32);
    h = mbjs_mem_handle_init(&a, p, 32, "borrowed-only");
    b = mbjs_mem_borrow(h, "reader");
    mbjs_mem_release(&a, &b);
    CHECK(mbjs_mem_handle_valid(b), "release refuses borrowed handles");
    mbjs_mem_abandon(&a, &b);

    mbjs_mem_arena_destroy(&a);
}

/* --- audit / leak detection ------------------------------------------------- */

static void test_audit(void)
{
    MBJSMemArena a;
    MBJSMemAudit r;
    void *p;
    MBJSMemHandle h;

    mbjs_mem_arena_init(&a, 0);

    p = mbjs_mem_arena_alloc(&a, 32);
    h = mbjs_mem_handle_init(&a, p, 32, "leak");
    r = mbjs_mem_arena_audit(&a);
    CHECK(r.handles_total == 1 && r.taken == 1 && r.bytes == 32,
          "audit reports an open TAKEN handle");

    mbjs_mem_release(&a, &h);
    r = mbjs_mem_arena_audit(&a);
    CHECK(r.handles_total == 0 && r.taken == 0,
          "audit is clean after release");

    /* A handle left TAKEN across reset is reported as leaked before the
     * registry is dropped by the reset itself. */
    p = mbjs_mem_arena_alloc(&a, 8);
    (void)mbjs_mem_handle_init(&a, p, 8, "forgotten");
    r = mbjs_mem_arena_audit(&a);
    CHECK(r.taken == 1, "audit flags a forgotten handle as a leak");

    mbjs_mem_arena_reset(&a);
    r = mbjs_mem_arena_audit(&a);
    CHECK(r.handles_total == 0, "reset clears the live-handle registry");

    mbjs_mem_arena_destroy(&a);
}

/* --- fixed-region arena (bare-metal / custom-OS mode) ----------------------- */

static void test_region(void)
{
    static uint8_t region[4u << 20]; /* 4 MiB .bss region, no malloc */
    MBJSMemArena a;
    MBJSMemAudit r;
    MBJSMemHandle h;
    void *p;
    int rc;

    /* Tiny region: allocations succeed until the region is exhausted, then
     * return NULL instead of growing via the system allocator. */
    {
        MBJSMemArena tiny;
        static uint8_t tiny_region[4096];
        int i, all_ok = 1;

        rc = mbjs_mem_arena_init_region(&tiny, tiny_region, sizeof(tiny_region));
        CHECK(rc == 0, "region arena initializes over a fixed buffer");
        for (i = 0; i < 80; i++) {
            if (mbjs_mem_arena_alloc(&tiny, 32) == NULL) {
                all_ok = 0;
                break;
            }
        }
        CHECK(all_ok, "region arena serves allocations from the region");
        /* Keep allocating until the 4 KiB region is full. */
        {
            int exhausted = 0;
            for (i = 0; i < 1000; i++) {
                if (mbjs_mem_arena_alloc(&tiny, 32) == NULL) {
                    exhausted = 1;
                    break;
                }
            }
            CHECK(exhausted,
                  "region exhaustion returns NULL (no system allocator)");
        }
        mbjs_mem_arena_reset(&tiny);
        CHECK(mbjs_mem_arena_alloc(&tiny, 64) != NULL,
              "region arena is reusable after reset");
        mbjs_mem_arena_destroy(&tiny);
    }

    /* Handles work on a region arena (no malloc behind the registry). */
    rc = mbjs_mem_arena_init_region(&a, region, sizeof(region));
    CHECK(rc == 0, "4 MiB region arena initializes");
    p = mbjs_mem_arena_alloc(&a, 64);
    h = mbjs_mem_handle_init(&a, p, 64, "region-handle");
    CHECK(mbjs_mem_handle_valid(h) && h.state == MBJS_MEM_TAKEN,
          "handles work on a region arena");
    mbjs_mem_release(&a, &h);
    r = mbjs_mem_arena_audit(&a);
    CHECK(r.handles_total == 0, "region arena audit is clean after release");

    /* The full engine runs on a region arena (bare-metal bootstrap path). */
    {
        MBJSRuntime *rt = MBJS_NewRuntime2(mbjs_mem_mf(), &a);
        MBJSContext *ctx;
        MBJSValue v;
        int32_t n = 0;

        CHECK(rt != NULL, "runtime boots on a fixed-region arena");
        if (rt != NULL) {
            ctx = MBJS_NewContext(rt);
            CHECK(ctx != NULL, "context created on region arena");
            if (ctx != NULL) {
                v = MBJS_Eval(ctx, "6 * 7", 5, "region.js", MBJS_EVAL_TYPE_GLOBAL);
                CHECK(!MBJS_IsException(v), "eval succeeds on region arena");
                if (!MBJS_IsException(v)) {
                    MBJS_ToInt32(ctx, &n, v);
                    CHECK(n == 42, "region-arena eval result is correct (42)");
                }
                MBJS_FreeValue(ctx, v);
                MBJS_FreeContext(ctx);
            }
            MBJS_FreeRuntime(rt);
        }
    }

    mbjs_mem_arena_destroy(&a);
    CHECK(a.magic != MBJS_MEM_ARENA_MAGIC, "region arena destroyed");
}

/* --- arena-backed MBJS runtime ---------------------------------------------- */

static void test_engine_arena(void)
{
    MBJSMemArena a;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSValue v;
    int32_t n = 0;
    MBJSMemAudit r;

    mbjs_mem_arena_init(&a, 0);

    rt = MBJS_NewRuntime2(mbjs_mem_mf(), &a);
    CHECK(rt != NULL, "runtime created with arena-backed allocator");

    ctx = MBJS_NewContext(rt);
    CHECK(ctx != NULL, "context created on arena runtime");

    v = MBJS_Eval(ctx, "1 + 2 * 3", 9, "test_mem.js", MBJS_EVAL_TYPE_GLOBAL);
    CHECK(!MBJS_IsException(v), "eval succeeds on arena runtime");
    if (!MBJS_IsException(v)) {
        MBJS_ToInt32(ctx, &n, v);
        CHECK(n == 7, "eval result is correct (7)");
    }
    MBJS_FreeValue(ctx, v);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);

    CHECK(mbjs_mem_arena_used(&a) > 0,
          "engine allocations flowed through the arena");
    r = mbjs_mem_arena_audit(&a);
    CHECK(r.handles_total == 0, "no explicit handles left open");

    mbjs_mem_arena_reset(&a);
    CHECK(mbjs_mem_arena_used(&a) == 0, "arena reset reclaimed the runtime");
    mbjs_mem_arena_destroy(&a);
}

int main(void)
{
    printf("== mbjs-mem tests ==\n");
    test_arena_basic();
    test_arena_limit();
    test_handles();
    test_audit();
    test_engine_arena();
    test_region();

    if (g_failures == 0) {
        printf("== ALL MEMORY TESTS PASSED ==\n");
        return 0;
    }
    printf("== %d MEMORY TEST(S) FAILED ==\n", g_failures);
    return 1;
}
