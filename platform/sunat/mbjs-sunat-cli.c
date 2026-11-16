/*
 * mbjs-sunat-cli.c — CLI for the Sunat GC build of MBJS (libmochiberry).
 *
 * The Sunat build is a runtime variant in which the garbage collector is
 * circumcised out: mbjs_trigger_gc() and MBJS_RunGC() are no-ops and every
 * byte the engine allocates lives in one deterministic mbjs-mem arena
 * (bump allocator, no per-object free) that is reclaimed in bulk at
 * teardown. This is the "memory management kosongan" model: explicit
 * ownership, zero GC pauses, memory = monotone bump, released in one shot.
 *
 * Usage:
 *   mbjs-sunat [-e EXPR | FILE] [--embed-arena]
 *
 *   --embed-arena  create the arena in the embedder (MBJS_NewRuntime2 +
 *                  mbjs_mem_mf()), then destroy it after MBJS_FreeRuntime
 *                  and audit the live-handle registry.
 */
#define MBJS_SUNAT_GC 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbjs.h"
#include "mbjs-libc.h"
#include "mbjs-mem.h"

static void print_sunat_stats(MBJSRuntime *rt, const char *tag)
{
    MBJSMemSunatStats st;

    MBJS_GetSunatStats(rt, &st);
    printf("sunat[%s]: gc_runs=%zu arena_used=%zu arena_peak=%zu "
           "arena_limit=%zu engine=%zu bytes in %zu allocs\n",
           tag, st.gc_runs, st.arena_used, st.arena_peak, st.arena_limit,
           st.engine_malloc_size, st.engine_malloc_count);
}

static int run_buf(MBJSContext *ctx, const char *buf, size_t len,
                   const char *filename)
{
    MBJSValue ret;

    ret = MBJS_Eval(ctx, buf, len, filename, 0);
    if (MBJS_IsException(ret)) {
        mbjs_std_dump_error(ctx);
        MBJS_FreeValue(ctx, ret);
        return 1;
    }
    if (!MBJS_IsUndefined(ret)) {
        const char *s = MBJS_ToCString(ctx, ret);
        if (s) {
            printf("%s\n", s);
            MBJS_FreeCString(ctx, s);
        } else {
            mbjs_std_dump_error(ctx);
        }
    }
    MBJS_FreeValue(ctx, ret);
    return 0;
}

static int run_file(MBJSContext *ctx, const char *filename)
{
    FILE *f;
    long len;
    char *buf;
    int ret;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(len + 1);
    if (!buf) {
        fclose(f);
        return 1;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return 1;
    }
    fclose(f);
    buf[len] = '\0';
    ret = run_buf(ctx, buf, (size_t)len, filename);
    free(buf);
    return ret;
}

static int run_with_runtime(int embed_arena, const char *expr,
                            const char *filename)
{
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSMemArena arena;
    MBJSMemArena *pa = NULL;
    int ret;

    if (embed_arena) {
        mbjs_mem_arena_init(&arena, 0);
        pa = &arena;
        rt = MBJS_NewRuntime2(mbjs_mem_mf(), pa);
    } else {
        rt = MBJS_NewRuntime(); /* sunat default: runtime-owned arena */
    }
    if (!rt) {
        fprintf(stderr, "mbjs-sunat: MBJS_NewRuntime failed\n");
        return 1;
    }
    print_sunat_stats(rt, "boot");

    ctx = MBJS_NewContext(rt);
    if (!ctx) {
        fprintf(stderr, "mbjs-sunat: MBJS_NewContext failed\n");
        MBJS_FreeRuntime(rt);
        return 1;
    }

    if (expr) {
        ret = run_buf(ctx, expr, strlen(expr), "<cmdline>");
    } else if (filename) {
        ret = run_file(ctx, filename);
    } else {
        fprintf(stderr, "usage: mbjs-sunat [-e EXPR | FILE] [--embed-arena]\n");
        ret = 1;
    }

    print_sunat_stats(rt, "end");

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt); /* destroys the runtime-owned arena if any */

    if (embed_arena && pa) {
        MBJSMemAudit audit;

        audit = mbjs_mem_arena_audit(pa);
        printf("sunat[audit]: handles=%zu taken=%zu borrowed=%zu bytes=%zu\n",
               audit.handles_total, audit.taken, audit.borrowed, audit.bytes);
        mbjs_mem_arena_destroy(pa);
        printf("sunat[reclaim]: arena used after destroy = %zu (expect 0)\n",
               mbjs_mem_arena_used(pa));
    }
    return ret;
}

int main(int argc, char **argv)
{
    const char *expr = NULL, *filename = NULL;
    int embed_arena = 0, i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-e")) {
            if (i + 1 < argc)
                expr = argv[++i];
        } else if (!strcmp(argv[i], "--embed-arena")) {
            embed_arena = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "mbjs-sunat: unknown option %s\n", argv[i]);
            return 1;
        } else {
            filename = argv[i];
        }
    }
    if (!expr && !filename) {
        fprintf(stderr, "usage: mbjs-sunat [-e EXPR | FILE] [--embed-arena]\n");
        return 1;
    }
    return run_with_runtime(embed_arena, expr, filename);
}
