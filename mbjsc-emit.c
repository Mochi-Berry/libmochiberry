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

static void dump_hex(FILE *f, const uint8_t *buf, size_t len)
{
    size_t i, col;
    col = 0;
    for(i = 0; i < len; i++) {
        fprintf(f, " 0x%02x,", buf[i]);
        if (++col == 8) {
            fprintf(f, "\n");
            col = 0;
        }
    }
    if (col != 0)
        fprintf(f, "\n");
}

void output_object_code(MBJSContext *ctx,
                               FILE *fo, MBJSValue obj, const char *c_name,
                               bool load_only)
{
    uint8_t *out_buf;
    size_t out_buf_len;
    int flags = MBJS_WRITE_OBJ_BYTECODE;

    if (strip) {
        flags |= MBJS_WRITE_OBJ_STRIP_SOURCE;
        if (strip > 1)
            flags |= MBJS_WRITE_OBJ_STRIP_DEBUG;
    }

    out_buf = MBJS_WriteObject(ctx, &out_buf_len, obj, flags);
    if (!out_buf) {
        mbjs_std_dump_error(ctx);
        exit(1);
    }

    namelist_add(&cname_list, c_name, NULL, load_only);

    if (output_type == OUTPUT_RAW) {
        fwrite(out_buf, 1, out_buf_len, fo);
    } else {
        fprintf(fo, "const uint32_t %s_size = %u;\n\n",
                c_name, (unsigned int)out_buf_len);
        fprintf(fo, "const uint8_t %s[%u] = {\n",
                c_name, (unsigned int)out_buf_len);
        dump_hex(fo, out_buf, out_buf_len);
        fprintf(fo, "};\n\n");
    }

    mbjs_free(ctx, out_buf);
}
