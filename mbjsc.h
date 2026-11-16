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
/*
 * mbjsc.h -- shared declarations for the MochiBerry compiler (mbjsc).
 *
 * The compiler used to be one 1500-line translation unit. It is split into
 * focused units that all include this header:
 *
 *   mbjsc.c            CLI: globals, help(), option parsing, main()
 *   mbjsc-namelist.c   namelist_* + get_c_name()
 *   mbjsc-emit.c       dump_hex() + output_object_code() (bytecode -> C array)
 *   mbjsc-pseudo.c     the built-in pseudo-C backend
 *   mbjsc-module.c     module loader (C modules / system modules)
 *   mbjsc-compile.c    compile_file(): one source file -> one output unit
 */
#ifndef MBJSC_H
#define MBJSC_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "mbjs.h"

typedef enum {
    OUTPUT_C,
    OUTPUT_C_MAIN,
    OUTPUT_RAW,
    OUTPUT_JIT,
} OutputTypeEnum;

/* Code-generation backend. `-o` (the ordinary form) selects the built-in
 * backend: a C-flavoured, decompiler-style rendering reconstructed from the
 * engine's own compiled representation (parser -> AST -> tree nodes ->
 * bytecode), which reads like reverse-engineered pseudo-C. `-o-llvm` selects
 * the MochiLLVM backend instead. */
typedef enum {
    BACKEND_PSEUDO_C, /* built-in: pseudo-C reconstructed from bytecode */
    BACKEND_LLVM,     /* MochiLLVM: JIT/AOT via dynamically loaded libLLVM  */
} BackendEnum;

typedef struct {
    char *name;
    char *short_name;
    int flags;
} namelist_entry_t;

typedef struct namelist_t {
    namelist_entry_t *array;
    int count;
    int size;
} namelist_t;

/* Rendering state of the pseudo-C backend. */
typedef struct {
    FILE *fo;
    int next_v;
    int depth;
    char *stack[256];
} PseudoCtx;

/* ---- shared state (defined in mbjsc.c) ------------------------------- */
extern namelist_t cname_list;
extern namelist_t cmodule_list;
extern namelist_t init_module_list;
extern OutputTypeEnum output_type;
extern BackendEnum backend;
extern int opt_addon;        /* berry optimization addon, on by default */
extern int opt_level;        /* O0..O10 ladder; -1 = not set */
extern int opt_level_given;
extern int opt_native;       /* target the host CPU (LLVM backend) */
extern int opt_lto;          /* run an LLVM ThinLTO pipeline (LLVM backend) */
extern int opt_keep_debug;
extern int opt_strip_given;  /* user passed --strip (redundant at O10) */
extern const char *superset; /* ECMAScript superset selector (--superset=) */
extern FILE *outfile;
extern const char *llvm_out_path; /* output path for the MochiLLVM backend */
extern const char *c_ident_prefix;
extern int strip;

/* ---- namelist / names (mbjsc-namelist.c) ----------------------------- */
void namelist_add(namelist_t *lp, const char *name, const char *short_name,
                  int flags);
void namelist_free(namelist_t *lp);
namelist_entry_t *namelist_find(namelist_t *lp, const char *name);
void get_c_name(char *buf, size_t buf_size, const char *file);

/* ---- bytecode emission (mbjsc-emit.c) -------------------------------- */
void output_object_code(MBJSContext *ctx, FILE *fo, MBJSValue obj,
                        const char *c_name, bool load_only);

/* ---- pseudo-C backend (mbjsc-pseudo.c) -------------------------------- */
void pseudo_emit_function(MBJSContext *ctx, FILE *fo, MBJSValue fn,
                           const char *filename, const char *cname);

/* ---- module loader (mbjsc-module.c) ---------------------------------- */
void find_unique_cname(char *cname, size_t cname_size);
MBJSModuleDef *jsc_module_loader(MBJSContext *ctx, const char *module_name,
                                 void *opaque);

/* ---- per-file compilation (mbjsc-compile.c) -------------------------- */
void compile_file(MBJSContext *ctx, FILE *fo, const char *filename,
                  const char *script_name, const char *c_name1, int module);

/* ---- CLI (mbjsc.c) ---------------------------------------------------- */
void help(void);

#endif /* MBJSC_H */
