/*
 * embed_libmochiberry.c — the "simple machine" host for libmochiberry.
 *
 * A deliberately tiny C program that proves the shared library works end to
 * end: it links libmochiberry.so, boots a full MBJS runtime whose allocator
 * is backed by the deterministic mbjs-mem arena (no GC for the C side), loads
 * a JavaScript application from disk and runs it.
 *
 * Build (see scripts/build-libmochiberry.sh):
 *   cc examples/embed_libmochiberry.c mbjs-mem.c -Idist/include \
 *      -Ldist -lmochiberry -Wl,-rpath,'$ORIGIN' -o dist/embed_libmochiberry -lm
 *
 * Run:
 *   dist/embed_libmochiberry examples/app_mochiberry.js
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbjs-mem.h"   /* arena + ownership-marked handles (host-side) */
#include "mbjs.h"       /* engine API, exported by libmochiberry.so */
#include "mbjs-libc.h"  /* std / os / console helpers, exported by the .so */

typedef struct EmbedState {
    MBJSMemArena arena;
    MBJSRuntime *rt;
    MBJSContext *ctx;
} EmbedState;

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static int run_app(EmbedState *st, const char *path)
{
    size_t len = 0;
    char *src = read_file(path, &len);
    MBJSValue ret;
    int rc = 0;
    int is_bytecode;

    if (src == NULL) {
        fprintf(stderr, "embed: could not read '%s'\n", path);
        return 1;
    }
    /* Bytecode files (mbjsc -b output, OTA app.mbc) start with the \x1bLx
     * magic; source files are evaluated normally. */
    is_bytecode = len >= 3 && (unsigned char)src[0] == 0x1b &&
                  (unsigned char)src[1] == 0x4c && (unsigned char)src[2] == 0x78;
    if (is_bytecode) {
        mbjs_std_eval_binary(st->ctx, (const uint8_t *)src, len, 0);
    } else {
        ret = MBJS_Eval(st->ctx, src, len, path, MBJS_EVAL_TYPE_GLOBAL);
        if (MBJS_IsException(ret)) {
            mbjs_std_dump_error(st->ctx);
            MBJS_FreeValue(st->ctx, ret);
            free(src);
            return 1;
        }
        MBJS_FreeValue(st->ctx, ret);
    }
    free(src);
    /* Drain promise microtasks and OS events scheduled by the app. */
    mbjs_std_loop(st->ctx);
    return rc;
}

int main(int argc, char **argv)
{
    EmbedState st;
    MBJSMemAudit audit;
    int rc;

    if (argc < 2) {
        fprintf(stderr, "usage: %s app.js [app-args...]\n", argv[0]);
        return 2;
    }

    memset(&st, 0, sizeof(st));

    /* Memory foundation: everything the engine allocates comes from this
     * deterministic arena. Individual frees are no-ops; the whole runtime is
     * reclaimed in bulk with one reset at the end. */
    mbjs_mem_arena_init(&st.arena, 0);
    mbjs_mem_arena_set_limit(&st.arena, 64 * 1024 * 1024); /* 64 MiB budget */

    st.rt = MBJS_NewRuntime2(mbjs_mem_mf(), &st.arena);
    if (st.rt == NULL) {
        fprintf(stderr, "embed: MBJS_NewRuntime2 failed\n");
        return 1;
    }
    mbjs_std_init_handlers(st.rt);
    MBJS_SetMemoryLimit(st.rt, 32 * 1024 * 1024); /* 32 MiB JS heap budget */

    st.ctx = MBJS_NewContext(st.rt);
    if (st.ctx == NULL) {
        fprintf(stderr, "embed: MBJS_NewContext failed\n");
        return 1;
    }
    mbjs_std_add_helpers(st.ctx, argc, argv);

    rc = run_app(&st, argv[1]);

    mbjs_std_free_handlers(st.rt);
    MBJS_FreeContext(st.ctx);
    MBJS_FreeRuntime(st.rt);

    printf("embed: arena peak %.2f MiB, %zu blocks\n",
           (double)mbjs_mem_arena_peak(&st.arena) / (1024.0 * 1024.0),
           st.arena.block_count);

    audit = mbjs_mem_arena_audit(&st.arena);
    if (audit.handles_total != 0) {
        fprintf(stderr, "embed: %zu handle(s) leaked (%zu taken, %zu borrowed)\n",
                audit.handles_total, audit.taken, audit.borrowed);
        rc = 1;
    }
    mbjs_mem_arena_destroy(&st.arena);
    return rc;
}
