/*
 * mbjs_berry.c — implementation of the MBJS Berry OS embed adapter.
 */

#include "mbjs_berry.h"

#include <string.h>

#include "mbjs-libc.h" /* mbjs_std_* helpers (part of libmochiberry) */

int mbjs_berry_init(MBJSBerryEngine *e, size_t heap_limit, size_t stack_limit)
{
    memset(e, 0, sizeof(*e));
    e->heap_limit = heap_limit ? heap_limit : MBJS_BERRY_HEAP_LIMIT;
    e->stack_limit = stack_limit ? stack_limit : MBJS_BERRY_STACK_LIMIT;

    /* Non-GC memory foundation: every engine allocation is carved from this
     * arena and reclaimed in bulk by mbjs_berry_destroy(). */
    mbjs_mem_arena_init(&e->arena, MBJS_BERRY_ARENA_BLOCK);
    mbjs_mem_arena_set_limit(&e->arena, e->heap_limit);

    e->rt = MBJS_NewRuntime2(mbjs_mem_mf(), &e->arena);
    if (e->rt == NULL)
        goto fail;
    mbjs_std_init_handlers(e->rt);
    MBJS_SetMemoryLimit(e->rt, e->heap_limit / 2); /* JS object budget */
    MBJS_SetMaxStackSize(e->rt, e->stack_limit);
    MBJS_SetGCThreshold(e->rt, MBJS_BERRY_GC_THRESHOLD);
    MBJS_SetDumpFlags(e->rt, MBJS_BERRY_DUMP_FLAGS);

    e->ctx = MBJS_NewContext(e->rt);
    if (e->ctx == NULL)
        goto fail;
    mbjs_std_add_helpers(e->ctx, 0, NULL);

    e->ready = 1;
    return 0;

fail:
    if (e->rt != NULL) {
        mbjs_std_free_handlers(e->rt);
        if (e->ctx != NULL)
            MBJS_FreeContext(e->ctx);
        MBJS_FreeRuntime(e->rt);
        e->rt = NULL;
        e->ctx = NULL;
    }
    mbjs_mem_arena_destroy(&e->arena);
    return -1;
}

int mbjs_berry_eval(MBJSBerryEngine *e, const char *src, size_t len,
                    const char *name)
{
    MBJSValue ret;

    if (e == NULL || !e->ready)
        return -1;
    ret = MBJS_Eval(e->ctx, src, len, name ? name : "<berry>",
                    MBJS_EVAL_TYPE_GLOBAL);
    if (MBJS_IsException(ret)) {
        mbjs_std_dump_error(e->ctx);
        MBJS_FreeValue(e->ctx, ret);
        return -1;
    }
    MBJS_FreeValue(e->ctx, ret);
    return 0;
}

int mbjs_berry_loop(MBJSBerryEngine *e)
{
    if (e == NULL || !e->ready)
        return -1;
    return mbjs_std_loop(e->ctx);
}

int mbjs_berry_eval_binary(MBJSBerryEngine *e, const uint8_t *buf, size_t len)
{
    if (e == NULL || !e->ready)
        return -1;
    if (buf == NULL || len == 0)
        return -1;
    /* The OTA path: the app is a compiled bytecode blob (mbjsc -b), the same
     * format dist/ota/app.mbc uses. beasm-built modules export the blob and
     * the loader calls this entry. */
    mbjs_std_eval_binary(e->ctx, buf, len, 0);
    if (MBJS_IsException(MBJS_GetException(e->ctx))) {
        mbjs_std_dump_error(e->ctx);
        return -1;
    }
    return mbjs_std_loop(e->ctx);
}

void mbjs_berry_destroy(MBJSBerryEngine *e)
{
    if (e == NULL)
        return;
    if (e->ready) {
        mbjs_std_free_handlers(e->rt);
        MBJS_FreeContext(e->ctx);
        MBJS_FreeRuntime(e->rt);
    }
    mbjs_mem_arena_destroy(&e->arena);
    memset(e, 0, sizeof(*e));
}
