/*
 * mbjs command line compiler
 *
 * Copyright (c) 2018-2026 Fabrice Bellard
 * Copyright (c) 2023-2026 Ben Noordhuis
 * Copyright (c) 2023-2026 Saúl Ibarra Corretgé
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "cutils.h"
#include "mbjs-libc.h"
#include "mbjs-llvm.h"
#include "mbjs-opt.h"
#include "mbjs-superset.h"

#include "mbjsc.h"

void compile_file(MBJSContext *ctx, FILE *fo,
                         const char *filename,
                         const char *script_name,
                         const char *c_name1,
                         int module)
{
    uint8_t *buf;
    char c_name[1024];
    int eval_flags;
    MBJSValue obj;
    size_t buf_len;

    buf = mbjs_load_file(ctx, &buf_len, filename);
    if (!buf) {
        fprintf(stderr, "Could not load '%s'\n", filename);
        exit(1);
    }
    if (output_type == OUTPUT_JIT) {
        /* Emit a C launcher that embeds the *source* and JIT-compiles it at
         * startup with the CC backend (mbjs-jit). Linking this with gcc and
         * running the result gives a native "JIT exe" that is compiled at
         * build time (the C launcher) but whose JS is lowered natively at
         * run time (JIT). */
        fprintf(fo, "/* JIT launcher for %s: embeds the source and JIT-compiles it. */\n",
                filename);
        fprintf(fo, "#include \"mbjs.h\"\n#include \"mbjs-libc.h\"\n#include \"mbjs-jit.h\"\n\n");
        fprintf(fo, "static const char jit_source[] =\n");
        fputc('"', fo);
        for (size_t i = 0; i < buf_len; i++) {
            unsigned char c = buf[i];
            if (c == '\n')
                fputs("\\n\"\n\"", fo);
            else if (c == '"')
                fputs("\\\"", fo);
            else if (c == '\\')
                fputs("\\\\", fo);
            else if (c >= 0x20 && c < 0x7f)
                fputc(c, fo);
            else
                fprintf(fo, "\\%03o", c);
        }
        fputs("\";\n", fo);
        fprintf(fo,
                "int main(int argc, char **argv)\n"
                "{\n"
                "  (void)argc; (void)argv;\n"
                "  MBJSRuntime *rt = MBJS_NewRuntime();\n"
                "  MBJSContext *ctx = MBJS_NewContext(rt);\n"
                "  MBJSJitOptions opts; MBJSJit *jit; MBJSJitFn fn; MBJSValue r;\n"
                "  mbjs_std_init_handlers(rt);\n"
                "  mbjs_std_add_helpers(ctx, argc, argv);\n"
                "  memset(&opts, 0, sizeof(opts));\n"
                "  opts.inc_dir = \"%s\";\n"
                "  jit = mbjs_jit_new(MBJS_JIT_BACKEND_CC, &opts);\n"
                "  if (!jit) { fprintf(stderr, \"mbjsc: no JIT\\n\"); return 1; }\n"
                "  fn = mbjs_jit_compile(jit, ctx, jit_source, \"<jit-exe>\");\n"
                "  if (!fn) {\n"
                "    fprintf(stderr, \"mbjsc: JIT compile failed: %%s\\n\",\n"
                "            mbjs_jit_last_error(jit));\n"
                "    r = MBJS_Eval(ctx, jit_source, sizeof(jit_source) - 1,\n"
                "                  \"<jit-exe>\", MBJS_EVAL_TYPE_GLOBAL);\n"
                "  } else {\n"
                "    r = fn(ctx, MBJS_UNDEFINED, 0, NULL);\n"
                "  }\n"
                "  if (MBJS_IsException(r)) mbjs_std_dump_error(ctx);\n"
                "  mbjs_jit_cleanup_all(jit, ctx);\n"
                "  mbjs_jit_free(jit);\n"
                "  mbjs_std_free_handlers(rt);\n"
                "  MBJS_FreeContext(ctx);\n"
                "  MBJS_FreeRuntime(rt);\n"
                "  return 0;\n"
                "}\n",
                getenv("MBJS_JIT_INC") ? getenv("MBJS_JIT_INC") : ".");
        mbjs_free(ctx, buf);
        return;
    }
    eval_flags = MBJS_EVAL_FLAG_COMPILE_ONLY;
    if (module < 0) {
        module = (mbjs__has_suffix(filename, ".mjs") ||
                  MBJS_DetectModule((const char *)buf, buf_len));
    }
    if (module)
        eval_flags |= MBJS_EVAL_TYPE_MODULE;
    else
        eval_flags |= MBJS_EVAL_TYPE_GLOBAL;
    obj = MBJS_Eval(ctx, (const char *)buf, buf_len, script_name ? script_name : filename, eval_flags);
    if (MBJS_IsException(obj)) {
        mbjs_std_dump_error(ctx);
        exit(1);
    }
    if (c_name1) {
        mbjs__pstrcpy(c_name, sizeof(c_name), c_name1);
    } else {
        get_c_name(c_name, sizeof(c_name), filename);
    }

    if (backend == BACKEND_LLVM && output_type == OUTPUT_C) {
        MBJSLLvm *llvm = mbjs_llvm_new(MBJS_LLVM_BACKEND_AOT);
        int ok = 0;
        if (llvm) {
            mbjs_llvm_set_opt_addon(llvm, opt_addon);
            mbjs_llvm_set_opt_level(llvm,
                                     opt_level_given ? opt_level : MBJS_OPT_DEFAULT);
            mbjs_llvm_set_native(llvm, opt_native);
            mbjs_llvm_set_lto(llvm, opt_lto);
            if (opt_native) {
                const char *feat = NULL;
                const char *cpu = mbjs_llvm_get_cpu(llvm, &feat);
                fprintf(stderr, "mbjsc: native CPU %s%s%s\n", cpu,
                        (feat && feat[0]) ? " (" : "",
                        (feat && feat[0]) ? feat : "");
            }
        }
        if (llvm && strcmp(mbjs_llvm_backend_name(llvm), "unavailable") != 0) {
            const char *path = llvm_out_path ? llvm_out_path : "out.o";
            if (mbjs_llvm_aot(llvm, ctx, (const char *)buf, filename, path,
                              c_name) == 0) {
                ok = 1;
                /* The LLVM Windows target emits MSVC-ABI unwind directives
                 * that GNU as cannot parse: drop them so the .s stays
                 * assemblable by the host toolchain. */
                if (mbjs__has_suffix(path, ".s"))
                    mbjs_opt_strip_unwind(path);
                /* O10 (The Stripper) implicitly strips the produced object
                 * when it has been written as a .o; for textual assembly it
                 * rewrites the file into "dead assembly" (no source
                 * identity, anonymous local labels). */
                if (mbjs_opt_implies_strip(opt_level) &&
                    mbjs_opt_implies_dead_asm(opt_level) &&
                    !opt_keep_debug) {
                    if (mbjs__has_suffix(path, ".o")) {
                        mbjs_opt_strip_binary(path);
                    } else if (mbjs__has_suffix(path, ".s")) {
                        if (mbjs_opt_dead_asm(path, path) == 0)
                            fprintf(stderr, "mbjsc: wrote dead assembly %s\n",
                                    path);
                        else
                            fprintf(stderr,
                                    "mbjsc: cannot rewrite %s as dead "
                                    "assembly\n",
                                    path);
                    }
                }
            } else {
                fprintf(stderr,
                        "mbjsc: MochiLLVM lowering failed (%s); falling back "
                        "to the built-in pseudo-C backend\n",
                        mbjs_llvm_last_error(llvm));
                /* The requested artifact is an object/assembly file. Writing
                 * pseudo-C text into it produces a file that looks like a
                 * compiled artifact but is neither: every later build step
                 * (assembler / linker) fails with a confusing error, and a
                 * stale .o from a previous run can even be linked in by
                 * mistake. Fail loudly instead and let the caller pick a
                 * .c output. */
                if (mbjs__has_suffix(path, ".o") ||
                    mbjs__has_suffix(path, ".s") ||
                    mbjs__has_suffix(path, ".obj")) {
                    fprintf(stderr,
                            "mbjsc: refusing to write pseudo-C into the "
                            "object/assembly file '%s'; "
                            "use a .c output for the pseudo-C backend\n",
                            path);
                    if (llvm)
                        mbjs_llvm_free(llvm);
                    mbjs_free(ctx, buf);
                    MBJS_FreeValue(ctx, obj);
                    exit(1);
                }
            }
        } else {
            fprintf(stderr,
                    "mbjsc: MochiLLVM unavailable (%s); falling back to the "
                    "built-in pseudo-C backend\n",
                    llvm ? mbjs_llvm_last_error(llvm) : "out of memory");
        }
        if (llvm)
            mbjs_llvm_free(llvm);
        if (ok) {
            mbjs_free(ctx, buf);
            MBJS_FreeValue(ctx, obj);
            return;
        }
        /* LLVM AOT failed: fall back to pseudo-C. `fo` is NULL (we deferred
         * opening it for this path), so open it now before rendering. */
        if (!fo) {
            const char *fname = llvm_out_path ? llvm_out_path : "out.c";
            fo = fopen(fname, "w");
            if (!fo) {
                perror(fname);
                exit(1);
            }
            outfile = fo;
            fprintf(fo, "/* File generated automatically by the MochiBerry compiler. */\n\n");
        }
    }

    mbjs_free(ctx, buf);
    if (output_type == OUTPUT_C && backend == BACKEND_PSEUDO_C) {
        pseudo_emit_function(ctx, fo, obj, filename, c_name);
    } else {
        output_object_code(ctx, fo, obj, c_name, false);
    }
    MBJS_FreeValue(ctx, obj);
}
