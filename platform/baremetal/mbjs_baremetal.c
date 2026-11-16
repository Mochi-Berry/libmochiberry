/*
 * mbjs_baremetal.c — implementation of the MBJS bare-metal / custom-OS
 * bootstrap.
 *
 * The whole engine boots inside one caller-owned RAM region:
 *
 *   region -> MBJSMemArena (fixed region, no malloc)
 *          -> MBJS_NewRuntime2(mbjs_mem_mf(), &arena)   (all engine memory)
 *          -> MBJS_NewContext
 *
 * Destroy rewinds the region: the same RAM can be re-seeded for a new
 * runtime, which is exactly what a custom OS wants for process/VM isolation.
 */

#include "mbjs_baremetal.h"

#ifdef MBJS_MEM_NO_LIBC
/* no-libc builds: tiny local zero/byte loops instead of memset/memcpy */
static void bm_set(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--)
        *p++ = (unsigned char)c;
}
#else
#include <string.h>
#define bm_set memset
#endif

int mbjs_baremetal_init(MBJSBaremetal *b, void *region, size_t region_size,
                        size_t heap_limit)
{
    if (b == NULL || region == NULL ||
        region_size < MBJS_BAREMETAL_MIN_REGION)
        return -1;
    bm_set(b, 0, sizeof(*b));

    if (mbjs_mem_arena_init_region(&b->arena, region, region_size) != 0)
        return -1;

    b->heap_limit = heap_limit ? heap_limit : MBJS_BAREMETAL_HEAP_LIMIT;
    if (b->heap_limit != 0 && b->heap_limit < b->arena.limit)
        mbjs_mem_arena_set_limit(&b->arena, b->heap_limit);
    else
        b->heap_limit = b->arena.limit;

    b->rt = MBJS_NewRuntime2(mbjs_mem_mf(), &b->arena);
    if (b->rt == NULL)
        goto fail;
    /* JS object budget: half the region, leaves headroom for the C side. */
    MBJS_SetMemoryLimit(b->rt, b->heap_limit / 2);
    MBJS_SetMaxStackSize(b->rt, MBJS_BAREMETAL_STACK_LIMIT);

    b->ctx = MBJS_NewContext(b->rt);
    if (b->ctx == NULL)
        goto fail;

    b->ready = 1;
    return 0;

fail:
    if (b->rt != NULL) {
        if (b->ctx != NULL) {
            MBJS_FreeContext(b->ctx);
            b->ctx = NULL;
        }
        MBJS_FreeRuntime(b->rt);
        b->rt = NULL;
    }
    mbjs_mem_arena_destroy(&b->arena);
    return -1;
}

int mbjs_baremetal_eval(MBJSBaremetal *b, const char *src, size_t len)
{
    MBJSValue ret;

    if (b == NULL || !b->ready || src == NULL)
        return -1;
    ret = MBJS_Eval(b->ctx, src, len, "<baremetal>", MBJS_EVAL_TYPE_GLOBAL);
    if (MBJS_IsException(ret)) {
        MBJS_FreeValue(b->ctx, ret);
        return -1;
    }
    MBJS_FreeValue(b->ctx, ret);
    return 0;
}

void mbjs_baremetal_destroy(MBJSBaremetal *b)
{
    if (b == NULL)
        return;
    if (b->ready) {
        if (b->ctx != NULL)
            MBJS_FreeContext(b->ctx);
        if (b->rt != NULL)
            MBJS_FreeRuntime(b->rt);
    }
    /* Rewind the region: every byte is available for the next boot. */
    mbjs_mem_arena_destroy(&b->arena);
    bm_set(b, 0, sizeof(*b));
}
