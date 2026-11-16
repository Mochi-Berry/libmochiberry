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

static int mbjs_module_dummy_init(MBJSContext *ctx, MBJSModuleDef *m)
{
    /* should never be called when compiling JS code */
    abort();
    return -1; // pacify compiler
}

void find_unique_cname(char *cname, size_t cname_size)
{
    char cname1[1024];
    int suffix_num;
    size_t len, max_len;
    assert(cname_size >= 32);
    /* find a C name not matching an existing module C name by
       adding a numeric suffix */
    len = strlen(cname);
    max_len = cname_size - 16;
    if (len > max_len)
        cname[max_len] = '\0';
    suffix_num = 1;
    for(;;) {
        snprintf(cname1, sizeof(cname1), "%s_%d", cname, suffix_num);
        if (!namelist_find(&cname_list, cname1))
            break;
        suffix_num++;
    }
    mbjs__pstrcpy(cname, cname_size, cname1);
}

MBJSModuleDef *jsc_module_loader(MBJSContext *ctx,
                              const char *module_name, void *opaque)
{
    MBJSModuleDef *m;
    namelist_entry_t *e;

    /* check if it is a declared C or system module */
    e = namelist_find(&cmodule_list, module_name);
    if (e) {
        /* add in the static init module list */
        namelist_add(&init_module_list, e->name, e->short_name, 0);
        /* create a dummy module */
        m = MBJS_NewCModule(ctx, module_name, mbjs_module_dummy_init);
    } else if (mbjs__has_suffix(module_name, ".so")) {
        MBJS_ThrowReferenceError(ctx, "%s: dynamically linking to shared libraries not supported",
        module_name);
        return NULL;
    } else {
        size_t buf_len;
        uint8_t *buf;
        MBJSValue func_val;
        char cname[1000];

        buf = mbjs_load_file(ctx, &buf_len, module_name);
        if (!buf) {
            MBJS_ThrowReferenceError(ctx, "could not load module filename '%s'",
                                   module_name);
            return NULL;
        }

        /* compile the module */
        func_val = MBJS_Eval(ctx, (char *)buf, buf_len, module_name,
                           MBJS_EVAL_TYPE_MODULE | MBJS_EVAL_FLAG_COMPILE_ONLY);
        mbjs_free(ctx, buf);
        if (MBJS_IsException(func_val))
            return NULL;
        get_c_name(cname, sizeof(cname), module_name);
        if (namelist_find(&cname_list, cname)) {
            find_unique_cname(cname, sizeof(cname));
        }
        /* Only the bytecode/embedding outputs (-b / -e) carry module blobs;
         * the pseudo-C and LLVM backends render the main unit only. */
        if (output_type != OUTPUT_C || backend != BACKEND_PSEUDO_C)
            output_object_code(ctx, outfile, func_val, cname, true);

        /* the module is already referenced, so we must free it */
        m = MBJS_VALUE_GET_PTR(func_val);
        MBJS_FreeValue(ctx, func_val);
    }
    return m;
}
