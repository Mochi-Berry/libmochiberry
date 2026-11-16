/* bench_aot.c -- AOT tier harness for MochiBerry.
 *
 * Measures the *real* AOT path: the script was compiled ahead of time by
 * `mbjsc -b` into a raw bytecode blob. At startup we only feed that blob to
 * mbjs_std_eval_binary() -- no parsing, no compiling.
 *
 * The script prints its own result marker (BENCH_RESULT <n>) via console.log
 * because eval_binary() has no completion value to hand back.
 *
 * usage: bench_aot <bytecode-file>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbjs.h"
#include "mbjs-libc.h"

#if defined(_WIN32)
#include <windows.h>
static double now_ms(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}
#else
#include <sys/time.h>
static double now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
#endif

int main(int argc, char **argv)
{
    MBJSRuntime *rt;
    MBJSContext *ctx;
    FILE *f;
    long len;
    unsigned char *code;
    double t0, t1;
    int r;

    if (argc < 2) {
        fprintf(stderr, "usage: bench_aot <bytecode-file>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "bench_aot: cannot open %s\n", argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    code = (unsigned char *)malloc(len > 0 ? (size_t)len : 1);
    if (!code) { fclose(f); return 2; }
    if (len > 0 && fread(code, 1, (size_t)len, f) != (size_t)len) {
        fclose(f); free(code); return 2;
    }
    fclose(f);

    rt = MBJS_NewRuntime();
    if (!rt) { free(code); return 2; }
    mbjs_std_init_handlers(rt);
    ctx = MBJS_NewContext(rt);
    if (!ctx) {
        mbjs_std_free_handlers(rt);
        MBJS_FreeRuntime(rt);
        free(code);
        return 2;
    }
    /* registers the `console` global (console.log etc.) */
    mbjs_std_add_helpers(ctx, 0, NULL);

    t0 = now_ms();
    mbjs_std_eval_binary(ctx, code, (size_t)len, 0);
    r = mbjs_std_loop(ctx);
    t1 = now_ms();
    if (r)
        mbjs_std_dump_error(ctx);

    printf("AOT_TIME_MS %.1f\n", t1 - t0);

    mbjs_std_free_handlers(rt);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
    free(code);
    return 0;
}
