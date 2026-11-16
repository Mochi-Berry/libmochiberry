/*
 * mbvm.c — MochiVM: the MochiBerry runner.
 *
 * mbvm is the counterpart of mbjsc (the compiler). mbjsc turns JavaScript
 * into bytecode (.mbc), C source or — with the AOT path — native code; mbvm
 * *runs* the result:
 *
 *   mbvm script.js            evaluate a JavaScript source (interpreted)
 *   mbvm program.mbc          load bytecode (MBJS_ReadObject + EvalFunction)
 *   mbvm --jit expr.js        compile an expression to native code via the JIT
 *                             tier (mbjs-jit) and call it directly
 *   mbvm --aot program.c      run a source file produced by mbjsc -aot
 *
 * Every runtime is backed by an MBJSMemArena (mbjs-mem): a deterministic,
 * non-GC arena allocator in the spirit of libmulchberry. The arena is passed
 * as the engine's allocator through MBJS_NewRuntime2(); at exit it is
 * audited so a leaked handle fails the run instead of leaking silently.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "mbjs.h"
#include "mbjs-libc.h"
#include "mbjs-mem.h"
#include "mbjs-jit.h"
#include "mbjs-llvm.h"

#define PROG_NAME "mbvm"

typedef enum RunKind {
    RUN_JS,   /* source: interpret */
    RUN_MBC,  /* bytecode blob */
    RUN_JIT,  /* source: compile+call native (CC backend) */
    RUN_JIT_LLVM, /* source: compile+call native (LLVM MCJIT backend) */
    RUN_AOT,  /* C source produced by mbjsc -aot (run as script) */
} RunKind;

static void help(void)
{
    printf("MochiVM (mbvm) version %s\n"
           "usage: " PROG_NAME " [options] file\n"
           "\n"
           "options are:\n"
           "  --jit         compile an expression to native code and call it\n"
           "  --jit-llvm    same, but use the LLVM MCJIT backend (libLLVM)\n"
           "  --aot         treat the input as AOT output (mbjsc -aot)\n"
           "  --mem-limit N set the arena memory budget to N bytes (0=unlimited)\n"
           "  --superset N  select a MochiBerry language superset (Takegawa TGPM)\n"
           "  --stack N     set the JS stack limit to N bytes\n"
           "  --no-await    do not await top-level promises\n"
           "  -h, --help    show this help\n",
           MBJS_GetVersion());
    exit(0);
}

static uint8_t *read_file(const char *path, size_t *plen)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long sz;
    if (!f) {
        fprintf(stderr, PROG_NAME ": cannot open '%s'\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[sz] = 0;
    *plen = (size_t)sz;
    fclose(f);
    return buf;
}

static bool has_suffix(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lf = strlen(suffix);
    return ls >= lf && strcmp(s + ls - lf, suffix) == 0;
}

int main(int argc, char **argv)
{
    int i, optind = 1;
    const char *input = NULL;
    const char *superset = NULL;
    RunKind kind = RUN_JS;
    size_t mem_limit = 0, stack_size = MBJS_DEFAULT_STACK_SIZE;
    bool do_await = true;
    MBJSMemArena arena;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSValue val = MBJS_UNDEFINED;
    MBJSJit *jit = NULL;
    uint8_t *buf = NULL;
    size_t buf_len = 0;
    int ret = 0;

    while (optind < argc) {
        const char *arg = argv[optind++];
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            help();
        } else if (!strcmp(arg, "--jit")) {
            kind = RUN_JIT;
        } else if (!strcmp(arg, "--jit-llvm")) {
            kind = RUN_JIT_LLVM;
        } else if (!strcmp(arg, "--aot")) {
            kind = RUN_AOT;
        } else if (!strcmp(arg, "--no-await")) {
            do_await = false;
        } else if (!strcmp(arg, "--mem-limit")) {
            if (optind >= argc) { fprintf(stderr, PROG_NAME ": missing value for --mem-limit\n"); return 1; }
            mem_limit = (size_t)strtoull(argv[optind++], NULL, 0);
        } else if (!strcmp(arg, "--stack")) {
            if (optind >= argc) { fprintf(stderr, PROG_NAME ": missing value for --stack\n"); return 1; }
            stack_size = (size_t)strtoull(argv[optind++], NULL, 0);
        } else if (!strcmp(arg, "--superset")) {
            if (optind >= argc) { fprintf(stderr, PROG_NAME ": missing value for --superset\n"); return 1; }
            superset = argv[optind++];
        } else if (!strncmp(arg, "--superset=", 11)) {
            superset = arg + 11;
        } else if (arg[0] == '-') {
            fprintf(stderr, PROG_NAME ": unknown option '%s'\n", arg);
            return 1;
        } else {
            input = arg;
        }
    }
    if (!input) {
        help();
        return 1;
    }

    /* Auto-detect .mbc bytecode unless --jit/--aot was given explicitly. */
    if (kind == RUN_JS && has_suffix(input, ".mbc"))
        kind = RUN_MBC;
    if (kind == RUN_JS && has_suffix(input, ".aot.c"))
        kind = RUN_AOT;

    /* Arena-backed runtime: every byte the engine allocates comes from the
     * deterministic arena (the libmulchberry-style memory manager). */
    mbjs_mem_arena_init(&arena, 0);
    mbjs_mem_arena_set_limit(&arena, mem_limit);

    rt = MBJS_NewRuntime2(mbjs_mem_mf(), &arena);
    if (!rt) {
        fprintf(stderr, PROG_NAME ": failed to create runtime\n");
        mbjs_mem_arena_destroy(&arena);
        return 1;
    }
    mbjs_std_init_handlers(rt);
    MBJS_SetMaxStackSize(rt, stack_size);
    if (superset) {
        if (MBJS_SetSuperset(rt, superset) < 0) {
            fprintf(stderr, PROG_NAME ": unknown superset '%s'\n", superset);
            ret = 1;
            goto end;
        }
    }
    ctx = MBJS_NewContext(rt);
    if (!ctx) {
        fprintf(stderr, PROG_NAME ": failed to create context\n");
        ret = 1;
        goto end;
    }
    mbjs_std_add_helpers(ctx, 0, NULL);

    buf = read_file(input, &buf_len);
    if (!buf) {
        ret = 1;
        goto end;
    }

    switch (kind) {
    case RUN_MBC:
        val = MBJS_ReadObject(ctx, buf, buf_len, MBJS_READ_OBJ_BYTECODE);
        if (MBJS_IsException(val)) {
            mbjs_std_dump_error(ctx);
            ret = 1;
            break;
        }
        if (!MBJS_IsFunction(ctx, val) &&
            MBJS_VALUE_GET_TAG(val) != MBJS_TAG_FUNCTION_BYTECODE &&
            MBJS_VALUE_GET_TAG(val) != MBJS_TAG_MODULE) {
            fprintf(stderr, PROG_NAME ": '%s' is not a function/module object\n", input);
            ret = 1;
            break;
        }
        val = MBJS_EvalFunction(ctx, val);
        if (MBJS_IsException(val)) {
            mbjs_std_dump_error(ctx);
            ret = 1;
        }
        break;

    case RUN_JIT: {
        /* Compile the source as an expression that evaluates to a callable and
         * dispatch to native code (mbjs-jit tier). Falls back to the
         * interpreter on any compile failure. */
        MBJSJitOptions jit_opts;
        const char *inc_dir, *lib_dir;
        memset(&jit_opts, 0, sizeof(jit_opts));
        inc_dir = getenv("MBJS_JIT_INC");
        lib_dir = getenv("MBJS_JIT_LIB");
        jit_opts.inc_dir = inc_dir;
        jit_opts.lib_dir = lib_dir;
        jit = mbjs_jit_new(MBJS_JIT_BACKEND_AUTO, &jit_opts);
        if (!jit) {
            fprintf(stderr, PROG_NAME ": failed to create JIT\n");
            ret = 1;
            break;
        }
        MBJSJitFn fn = mbjs_jit_compile(jit, ctx, (const char *)buf, input);
        if (!fn) {
            fprintf(stderr, PROG_NAME ": JIT compile failed (%s); falling back\n",
                    mbjs_jit_last_error(jit));
            val = MBJS_Eval(ctx, (const char *)buf, buf_len, input,
                            MBJS_EVAL_TYPE_GLOBAL);
        } else {
            val = fn(ctx, MBJS_UNDEFINED, 0, NULL);
        }
        if (MBJS_IsException(val)) {
            mbjs_std_dump_error(ctx);
            ret = 1;
        }
        break;
    }

    case RUN_JIT_LLVM: {
        /* Compile an integer expression via the LLVM MCJIT backend and call
         * the native entry. Falls back to the interpreter on failure. */
        MBJSLLvm *llvm = mbjs_llvm_new(MBJS_LLVM_BACKEND_JIT);
        if (!llvm || strcmp(mbjs_llvm_backend_name(llvm), "unavailable") == 0) {
            fprintf(stderr, PROG_NAME ": LLVM JIT unavailable (%s); falling back\n",
                    llvm ? mbjs_llvm_last_error(llvm) : "out of memory");
            val = MBJS_Eval(ctx, (const char *)buf, buf_len, input,
                            MBJS_EVAL_TYPE_GLOBAL);
        } else {
            MBJSJitFn fn = mbjs_llvm_jit(llvm, ctx, (const char *)buf, input);
            if (!fn) {
                fprintf(stderr, PROG_NAME ": LLVM JIT compile failed (%s); "
                        "falling back\n", mbjs_llvm_last_error(llvm));
                val = MBJS_Eval(ctx, (const char *)buf, buf_len, input,
                                MBJS_EVAL_TYPE_GLOBAL);
            } else {
                val = fn(ctx, MBJS_UNDEFINED, 0, NULL);
            }
        }
        if (llvm)
            mbjs_llvm_free(llvm);
        if (MBJS_IsException(val)) {
            mbjs_std_dump_error(ctx);
            ret = 1;
        }
        break;
    }

    case RUN_AOT:
    case RUN_JS:
    default:
        val = MBJS_Eval(ctx, (const char *)buf, buf_len, input,
                        MBJS_EVAL_TYPE_GLOBAL);
        if (MBJS_IsException(val)) {
            mbjs_std_dump_error(ctx);
            ret = 1;
        }
        break;
    }

    if (!MBJS_IsException(val) && do_await) {
        MBJSValue awaited = mbjs_std_await(ctx, val);
        if (MBJS_IsException(awaited)) {
            mbjs_std_dump_error(ctx);
            ret = 1;
        }
        MBJS_FreeValue(ctx, awaited);
    }

end:
    if (buf)
        free(buf);
    if (jit) {
        mbjs_jit_cleanup_all(jit, ctx);
        mbjs_jit_free(jit);
    }
    if (ctx)
        MBJS_FreeContext(ctx);
    if (rt)
        MBJS_FreeRuntime(rt);

    /* Arena audit: any still-TAKEN handle is a leak we turn into a failure. */
    if (mbjs_mem_arena_audit(&arena).taken > 0) {
        fprintf(stderr, PROG_NAME ": arena leak detected (%zu taken handles)\n",
                mbjs_mem_arena_audit(&arena).taken);
        if (ret == 0)
            ret = 1;
    }
    mbjs_mem_arena_destroy(&arena);
    return ret;
}
