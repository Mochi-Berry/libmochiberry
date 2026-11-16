/*
 * embed_bytecode.c — host that runs a *transpiled* bytecode module.
 *
 * The transpilers (scripts/transpile-c.sh, scripts/transpile-asm.sh) turn a
 * JavaScript app into freestanding C or inline-assembly data (the app's
 * compiled bytecode, produced by mbjsc). This host declares that data and
 * runs it through the OTA-style bytecode path (mbjs_std_eval_binary), so a
 * transpiled app behaves exactly like an OTA app.
 *
 * The generated module is expected to define:
 *   extern const uint32_t mbjsc_app_size;   (bytecode length)
 *   extern const uint8_t  mbjsc_app[];      (bytecode bytes)
 *
 * Link (see scripts/transpile-c.sh):
 *   cc app.c embed_bytecode.c -Ldist -lmochiberry -Wl,-rpath,$PWD/dist -o app
 *   cc app.S embed_bytecode.c -Ldist -lmochiberry -Wl,-rpath,$PWD/dist -o app
 */

#include <stdio.h>
#include <string.h>

#include "mbjs-mem.h"
#include "mbjs.h"
#include "mbjs-libc.h"

extern const uint32_t mbjsc_app_size;
extern const uint8_t mbjsc_app[];

int main(void)
{
    MBJSMemArena arena;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    int rc = 0;

    mbjs_mem_arena_init(&arena, 0);
    mbjs_mem_arena_set_limit(&arena, 64u * 1024u * 1024u);

    rt = MBJS_NewRuntime2(mbjs_mem_mf(), &arena);
    if (rt == NULL) {
        fprintf(stderr, "embed_bytecode: no runtime\n");
        return 1;
    }
    mbjs_std_init_handlers(rt);
    ctx = MBJS_NewContext(rt);
    if (ctx == NULL) {
        fprintf(stderr, "embed_bytecode: no context\n");
        mbjs_std_free_handlers(rt);
        MBJS_FreeRuntime(rt);
        mbjs_mem_arena_destroy(&arena);
        return 1;
    }
    mbjs_std_add_helpers(ctx, 0, NULL);

    /* The transpiled app: bytecode tables (freestanding data), evaluated
     * through the same path OTA apps use. */
    mbjs_std_eval_binary(ctx, mbjsc_app, mbjsc_app_size, 0);
    if (MBJS_IsException(MBJS_GetException(ctx))) {
        mbjs_std_dump_error(ctx);
        rc = 1;
    }
    mbjs_std_loop(ctx);

    mbjs_std_free_handlers(rt);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
    mbjs_mem_arena_destroy(&arena);
    return rc;
}
