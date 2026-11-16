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

namelist_t cname_list;
namelist_t cmodule_list;
namelist_t init_module_list;
OutputTypeEnum output_type;
BackendEnum backend = BACKEND_PSEUDO_C;
int opt_addon = 1; /* berry optimization addon, on by default */
int opt_level = MBJS_OPT_MAX; /* O0..O10 ladder; -1 = not set */
int opt_level_given = 0;
int opt_native = 0;   /* target the host CPU (LLVM backend) */
int opt_lto = 0;      /* run an LLVM ThinLTO pipeline (LLVM backend) */
int opt_keep_debug = 0;
int opt_strip_given = 0; /* user passed --strip (redundant at O10) */
const char *superset; /* ECMAScript superset selector (--superset=) */
FILE *outfile;
const char *llvm_out_path; /* output path for the MochiLLVM backend */
const char *c_ident_prefix = "mbjsc_";
int strip;

static const char main_c_template1[] =
    "int main(int argc, char **argv)\n"
    "{\n"
    "  int r;\n"
    "  MBJSRuntime *rt;\n"
    "  MBJSContext *ctx;\n"
    "  r = 0;\n"
    "  rt = MBJS_NewRuntime();\n"
    "  mbjs_std_set_worker_new_context_func(MBJS_NewCustomContext);\n"
    "  mbjs_std_init_handlers(rt);\n"
    ;

static const char main_c_template2[] =
    "  r = mbjs_std_loop(ctx);\n"
    "  if (r) {\n"
    "    mbjs_std_dump_error(ctx);\n"
    "  }\n"
    "  mbjs_std_free_handlers(rt);\n"
    "  MBJS_FreeContext(ctx);\n"
    "  MBJS_FreeRuntime(rt);\n"
    "  return r;\n"
    "}\n";

#define PROG_NAME "mbjsc"

void help(void)
{
    printf("MochiBerry compiler version %s\n"
           "usage: " PROG_NAME " [options] [files]\n"
           "\n"
           "options are:\n"
           "-b          output raw bytecode instead of C code\n"
           "-e          output main() and bytecode in a C file\n"
           "-j          output a JIT launcher C file (JIT-compiles at startup)\n"
           "-o output   set the output filename (built-in pseudo-C backend)\n"
           "-o-llvm     use the MochiLLVM codegen (JIT/AOT) instead of pseudo-C\n"
           "-o-c        select the built-in pseudo-C backend explicitly\n"
           "--no-opt-addon     disable the berry optimization addon (JIT/AOT)\n"
           "--opt-addon        enable it (default; typically faster than -O3)\n"
           "--superset=NAME    select a strict ECMAScript superset (typescript, jsx, ...)\n"
           "--out=FILE         set the output filename (same as -o FILE)\n"
           "-n script_name    set the script name (as used in stack traces)\n"
           "-N cname    set the C name of the generated data\n"
           "-C          compile as JS classic script (default=autodetect)\n"
           "-m          compile as ES module (default=autodetect)\n"
           "-D module_name         compile a dynamically loaded module or worker\n"
           "-M module_name[,cname] add initialization code for an external C module\n"
           "-p prefix   set the prefix of the generated C names\n"
           "-P          do not add default system modules\n"
           "-s          strip the source code, specify twice to also strip debug info\n"
           "-O<n>       optimization level 0..10 (Raw..The Stripper; default 10)\n"
            "--native    target the host CPU (LLVM host CPU + features)\n"
            "--lto       run an LLVM ThinLTO pipeline before emission\n"
           "--keep-debug  keep debug info even at -O10\n"
           "-S n        set the maximum stack size to 'n' bytes (default=%d)\n",
           MBJS_GetVersion(),
           MBJS_DEFAULT_STACK_SIZE);
    exit(1);
}

// TODO(bnoordhuis) share with mbjs.c maybe
static int64_t parse_limit(const char *arg) {
    char *p;
    unsigned long unit = 1; // bytes for backcompat; mbjs defaults to kilobytes
    double d = strtod(arg, &p);

    if (p == arg) {
        fprintf(stderr, "mbjsc: invalid limit: %s\n", arg);
        return -1;
    }

    if (*p) {
        switch (*p++) {
        case 'b': case 'B': unit = 1UL <<  0; break;
        case 'k': case 'K': unit = 1UL << 10; break; /* IEC kibibytes */
        case 'm': case 'M': unit = 1UL << 20; break; /* IEC mebibytes */
        case 'g': case 'G': unit = 1UL << 30; break; /* IEC gigibytes */
        default:
            fprintf(stderr, "mbjsc: invalid limit: %s, unrecognized suffix, only k,m,g are allowed\n", arg);
            return -1;
        }
        if (*p) {
            fprintf(stderr, "mbjsc: invalid limit: %s, only one suffix allowed\n", arg);
            return -1;
        }
    }

    return (int64_t)(d * unit);
}

static void check_hasarg(int optind, int argc, int opt)
{
    if (optind >= argc) {
        fprintf(stderr, "mbjsc: missing file for -%c\n", opt);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    int optind = 1;
    int i;
    const char *out_filename, *cname, *script_name;
    char cfilename[1024];
    FILE *fo;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    int module;
    size_t stack_size;
    namelist_t dynamic_module_list;
    bool load_system_modules = true;

    out_filename = NULL;
    script_name = NULL;
    output_type = OUTPUT_C;
    cname = NULL;
    module = -1;
    strip = 0;
    stack_size = 0;
    memset(&dynamic_module_list, 0, sizeof(dynamic_module_list));


    while (optind < argc && *argv[optind] == '-') {
        char *arg = argv[optind] + 1;
        char *longopt = "";
        char *optarg = NULL;
        /* a single - is not an option, it also stops argument scanning */
        if (!*arg)
            break;
        optind++;
        if (*arg == '-') {
            longopt = arg + 1;
            optarg = strchr(longopt, '=');
            if (optarg)
                *optarg++ = '\0';
            arg += strlen(arg);
            /* -- stops argument scanning */
            if (!*longopt)
                break;
        }
        for (; *arg || *longopt; longopt = "") {
            char opt = *arg;
            if (opt) {
                arg++;
                if (!optarg && *arg)
                    optarg = arg;
            }
            if (opt == 'h' || opt == '?' || !strcmp(longopt, "help")) {
                help();
                continue;
            }
            if (!strcmp(longopt, "no-opt-addon")) {
                opt_addon = 0;
                continue;
            }
            if (!strcmp(longopt, "opt-addon")) {
                opt_addon = 1;
                continue;
            }
            if (!strcmp(longopt, "llvm")) {
                backend = BACKEND_LLVM;
                continue;
            }
            if (!strcmp(longopt, "pseudo") || !strcmp(longopt, "c")) {
                backend = BACKEND_PSEUDO_C;
                continue;
            }
            if (!strcmp(longopt, "superset")) {
                if (!optarg) {
                    check_hasarg(optind, argc, opt);
                    optarg = argv[optind++];
                }
                superset = optarg;
                continue;
            }
            if (!strcmp(longopt, "out")) {
                if (!optarg) {
                    check_hasarg(optind, argc, opt);
                    optarg = argv[optind++];
                }
                out_filename = optarg;
                continue;
            }
            if (opt == 'b') {
                output_type = OUTPUT_RAW;
                continue;
            }
            if (opt == 'j') {
                output_type = OUTPUT_JIT;
                continue;
            }
            if (opt == 'o') {
                /* `-o-<backend>` selects the code generator; a bare `-o FILE`
                 * (or the attached `-oFILE`) still names the output file and
                 * uses the built-in pseudo-C backend. */
                if (optarg && optarg[0] == '-') {
                    if (!strcmp(optarg, "-llvm")) {
                        backend = BACKEND_LLVM;
                    } else if (!strcmp(optarg, "-c") ||
                               !strcmp(optarg, "-pseudo")) {
                        backend = BACKEND_PSEUDO_C;
                    } else {
                        fprintf(stderr, "mbjsc: unknown output backend: %s\n",
                                optarg);
                        exit(1);
                    }
                    arg = ""; /* consume the attached -o-<backend> token */
                    continue;
                }
                if (!optarg) {
                    check_hasarg(optind, argc, opt);
                    optarg = argv[optind++];
                }
                out_filename = optarg;
                continue;
            }
            if (opt == 'e') {
                output_type = OUTPUT_C_MAIN;
                continue;
            }
            if (opt == 'n') {
                if (!optarg) {
                    check_hasarg(optind, argc, opt);
                    optarg = argv[optind++];
                }
                script_name = optarg;
                continue;
            }
            if (opt == 'N') {
                if (!optarg) {
                    check_hasarg(optind, argc, opt);
                    optarg = argv[optind++];
                }
                cname = optarg;
                continue;
            }
            if (opt == 'C') {
                module = 0;
                continue;
            }
            if (opt == 'm') {
                module = 1;
                continue;
            }
            if (opt == 'M') {
                char *p;
                char path[1024];
                char cname[1024];
                if (!optarg) {
                    check_hasarg(optind, argc, opt);
                    optarg = argv[optind++];
                }
                mbjs__pstrcpy(path, sizeof(path), optarg);
                p = strchr(path, ',');
                if (p) {
                    *p = '\0';
                    mbjs__pstrcpy(cname, sizeof(cname), p + 1);
                } else {
                    get_c_name(cname, sizeof(cname), path);
                }
                namelist_add(&cmodule_list, path, cname, 0);
                continue;
            }
            if (opt == 'D') {
                if (!optarg) {
                    check_hasarg(optind, argc, opt);
                    optarg = argv[optind++];
                }
                namelist_add(&dynamic_module_list, optarg, NULL, 0);
                continue;
            }
            if (opt == 'P') {
                load_system_modules = false;
                continue;
            }
            if (opt == 's') {
                strip++;
                continue;
            }
            if (opt == 'O' || !strcmp(longopt, "opt-level")) {
                if (!optarg) {
                    check_hasarg(optind, argc, opt);
                    optarg = argv[optind++];
                }
                if (!strcmp(optarg, "s") || !strcmp(optarg, "z")) {
                    /* -Os / -Oz: map onto the nearest ladder level. */
                    opt_level = (strcmp(optarg, "z") == 0) ? 1 : 2;
                } else {
                    char *end = NULL;
                    long v = strtol(optarg, &end, 10);
                    if (end == optarg || (end && *end != '\0') ||
                        v < MBJS_OPT_MIN || v > MBJS_OPT_MAX) {
                        fprintf(stderr,
                                "mbjsc: invalid optimization level '%s' "
                                "(expected 0..%d)\n",
                                optarg, MBJS_OPT_MAX);
                        exit(1);
                    }
                    opt_level = (int)v;
                }
                opt_level_given = 1;
                /* The level digits were consumed as the option argument; the
                 * outer loop would otherwise treat them as further flags. */
                if (*arg) {
                    *arg = '\0';
                    longopt = "";
                }
                continue;
            }
            if (!strcmp(longopt, "native")) {
                opt_native = 1;
                continue;
            }
            if (!strcmp(longopt, "lto")) {
                opt_lto = 1;
                continue;
            }
            if (!strcmp(longopt, "keep-debug")) {
                opt_keep_debug = 1;
                continue;
            }
            if (!strcmp(longopt, "strip") || !strcmp(longopt, "strip-all")) {
                opt_strip_given = 1;
                continue;
            }
            if (opt == 'p') {
                if (!optarg) {
                    check_hasarg(optind, argc, opt);
                    optarg = argv[optind++];
                }
                c_ident_prefix = optarg;
                continue;
            }
            if (opt == 'S') {
                if (!optarg) {
                    check_hasarg(optind, argc, opt);
                    optarg = argv[optind++];
                }
                stack_size = parse_limit(optarg);
                continue;
            }
            help();
        }
    }

    if (load_system_modules) {
        /* add system modules */
        namelist_add(&cmodule_list, "mbjs:std", "std", 0);
        namelist_add(&cmodule_list, "mbjs:os", "os", 0);
        namelist_add(&cmodule_list, "mbjs:bjson", "bjson", 0);
        namelist_add(&cmodule_list, "std", "std", 0);
        namelist_add(&cmodule_list, "os", "os", 0);
        namelist_add(&cmodule_list, "bjson", "bjson", 0);
    }

    if (optind >= argc) {
        fprintf(stderr,
                "mbjsc: fatal error: no available command or input file\n"
                "compilation idled.\n"
                "please use --help or -h for more tips.\n");
        exit(1);
    }

    if (superset && *superset && strcmp(superset, MBJS_SUPERSET_DEFAULT)) {
        if (!mbjs_superset_lookup(superset)) {
            fprintf(stderr, "mbjsc: unknown superset '%s'\n", superset);
            exit(1);
        }
    }

    if (!opt_level_given)
        opt_level = MBJS_OPT_DEFAULT;
    opt_level = mbjs_opt_clamp(opt_level);

    /* Warn about flags that the chosen level already implies. */
    mbjs_opt_warn_redundant(opt_level, opt_strip_given, 0);
    if (opt_keep_debug && mbjs_opt_implies_no_debug(opt_level))
        fprintf(stderr,
                "mbjsc: warning: '--keep-debug' conflicts with -O%d (no debug "
                "info is emitted); use -O9 for debuggable output\n",
                opt_level);

    if (!out_filename)
        out_filename = (backend == BACKEND_LLVM) ? "out.o" : "out.c";
    llvm_out_path = out_filename;

    mbjs__pstrcpy(cfilename, sizeof(cfilename), out_filename);

    if (output_type == OUTPUT_RAW)
        fo = fopen(cfilename, "wb");
    else if (backend == BACKEND_LLVM && output_type == OUTPUT_C)
        fo = NULL; /* LLVM AOT writes the object directly; opened only on fallback */
    else
        fo = fopen(cfilename, "w");

    if (output_type != OUTPUT_RAW && fo == NULL &&
        !(backend == BACKEND_LLVM && output_type == OUTPUT_C)) {
        perror(cfilename);
        exit(1);
    }
    outfile = fo;

    rt = MBJS_NewRuntime();
    if (superset)
        MBJS_SetSuperset(rt, superset);
    ctx = MBJS_NewContext(rt);

    /* loader for ES6 modules */
    MBJS_SetModuleLoaderFunc(rt, NULL, jsc_module_loader, NULL);

    if (output_type != OUTPUT_RAW && fo) {
        fprintf(fo, "/* File generated automatically by the MochiBerry compiler. */\n");
        if (superset && *superset && strcmp(superset, MBJS_SUPERSET_DEFAULT))
            fprintf(fo, "/* superset: %s */\n", superset);
        fprintf(fo, "\n");
    }

    if (output_type == OUTPUT_C_MAIN) {
        fprintf(fo, "#include \"mbjs-libc.h\"\n");
        if (superset && *superset && strcmp(superset, MBJS_SUPERSET_DEFAULT))
            fprintf(fo, "#define MBJS_SUPERSET \"%s\"\n", superset);
        fprintf(fo, "\n");
    } else if (output_type == OUTPUT_C && backend == BACKEND_PSEUDO_C) {
        fprintf(fo, "/* backend: built-in pseudo-C (decompiler style)\n"
                    " * use -b for raw bytecode or -e for embeddable C + main */\n"
                    "\n");
    } else if (output_type == OUTPUT_C && fo) {
        fprintf(fo, "#include <inttypes.h>\n"
                "\n"
                );
    }

    for(i = optind; i < argc; i++) {
        const char *filename = argv[i];
        compile_file(ctx, fo, filename, script_name, cname, module);
        cname = NULL;
    }

    for(i = 0; i < dynamic_module_list.count; i++) {
        if (!jsc_module_loader(ctx, dynamic_module_list.array[i].name, NULL)) {
            fprintf(stderr, "Could not load dynamic module '%s'\n",
                    dynamic_module_list.array[i].name);
            exit(1);
        }
    }

    if (output_type == OUTPUT_C_MAIN) {
        fprintf(fo,
                "static MBJSContext *MBJS_NewCustomContext(MBJSRuntime *rt)\n"
                "{\n"
                "  MBJSContext *ctx = MBJS_NewContext(rt);\n"
                "  if (!ctx)\n"
                "    return NULL;\n");
        /* add the precompiled modules (XXX: could modify the module
           loader instead) */
        for(i = 0; i < init_module_list.count; i++) {
            namelist_entry_t *e = &init_module_list.array[i];
            /* initialize the static C modules */

            fprintf(fo,
                    "  {\n"
                    "    extern MBJSModuleDef *mbjs_init_module_%s(MBJSContext *ctx, const char *name);\n"
                    "    mbjs_init_module_%s(ctx, \"%s\");\n"
                    "  }\n",
                    e->short_name, e->short_name, e->name);
        }
        for(i = 0; i < cname_list.count; i++) {
            namelist_entry_t *e = &cname_list.array[i];
            if (e->flags) {
                fprintf(fo, "  mbjs_std_eval_binary(ctx, %s, %s_size, 1);\n",
                        e->name, e->name);
            }
        }
        fprintf(fo,
                "  return ctx;\n"
                "}\n\n");

        fputs(main_c_template1, fo);

        if (stack_size != 0) {
            fprintf(fo, "  MBJS_SetMaxStackSize(rt, %u);\n",
                    (unsigned int)stack_size);
        }

        /* add the module loader */
        fprintf(fo, "  MBJS_SetModuleLoaderFunc2(rt, NULL, mbjs_module_loader, mbjs_module_check_attributes, NULL);\n");

        fprintf(fo,
                "  ctx = MBJS_NewCustomContext(rt);\n"
                "  mbjs_std_add_helpers(ctx, argc, argv);\n");

        for(i = 0; i < cname_list.count; i++) {
            namelist_entry_t *e = &cname_list.array[i];
            if (!e->flags) {
                fprintf(fo, "  mbjs_std_eval_binary(ctx, %s, %s_size, 0);\n",
                        e->name, e->name);
            }
        }
        fputs(main_c_template2, fo);
    }

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);

    if (fo)
        fclose(fo);

    namelist_free(&cname_list);
    namelist_free(&cmodule_list);
    namelist_free(&init_module_list);
    return 0;
}
