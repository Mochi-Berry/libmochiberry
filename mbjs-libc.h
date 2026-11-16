/*
 * mbjs C library
 *
 * Copyright (c) 2017-2018 Fabrice Bellard
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
#ifndef mbjs_LIBC_H
#define mbjs_LIBC_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "mbjs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t *MBJSLoadFileFunc(MBJSContext *ctx, size_t *pbuf_len,
                                const char *filename);

MBJS_LIBC_EXTERN MBJSModuleDef *mbjs_init_module_std(MBJSContext *ctx,
                                               const char *module_name);
MBJS_LIBC_EXTERN MBJSModuleDef *mbjs_init_module_os(MBJSContext *ctx,
                                              const char *module_name);
MBJS_LIBC_EXTERN MBJSModuleDef *mbjs_init_module_bjson(MBJSContext *ctx,
                                                 const char *module_name);
MBJS_LIBC_EXTERN void mbjs_std_add_helpers(MBJSContext *ctx, int argc, char **argv);
MBJS_LIBC_EXTERN int mbjs_load_patches(MBJSContext *ctx, const char *dir);
MBJS_LIBC_EXTERN int mbjs_std_loop(MBJSContext *ctx);
MBJS_LIBC_EXTERN int mbjs_std_loop_once(MBJSContext *ctx);
MBJS_LIBC_EXTERN int mbjs_std_poll_io(MBJSContext *ctx, int timeout_ms);
MBJS_LIBC_EXTERN MBJSValue mbjs_std_await(MBJSContext *ctx, MBJSValue obj);
MBJS_LIBC_EXTERN void mbjs_std_init_handlers(MBJSRuntime *rt);
MBJS_LIBC_EXTERN void mbjs_std_free_handlers(MBJSRuntime *rt);
MBJS_LIBC_EXTERN void mbjs_std_dump_error(MBJSContext *ctx);
MBJS_LIBC_EXTERN uint8_t *mbjs_load_file(MBJSContext *ctx, size_t *pbuf_len,
                                     const char *filename);
MBJS_LIBC_EXTERN int mbjs_module_set_import_meta(MBJSContext *ctx, MBJSValueConst func_val,
                                             bool use_realpath, bool is_main);
MBJS_LIBC_EXTERN MBJSModuleDef *mbjs_module_loader(MBJSContext *ctx,
                                             const char *module_name, void *opaque,
                                             MBJSValueConst attributes);
// like mbjs_module_loader but does not load .so objects and the file reader
// is pluggable; mbjs_module_loader is implemented in terms of mbjs_module_load
MBJS_LIBC_EXTERN MBJSModuleDef *mbjs_module_load(MBJSContext *ctx, const char *module_name,
                                           void *opaque, MBJSValueConst attributes,
                                           MBJSLoadFileFunc *load_file);
MBJS_LIBC_EXTERN int mbjs_module_check_attributes(MBJSContext *ctx, void *opaque,
                                              MBJSValueConst attributes);
MBJS_LIBC_EXTERN void mbjs_std_eval_binary(MBJSContext *ctx, const uint8_t *buf,
                                       size_t buf_len, int flags);
MBJS_LIBC_EXTERN void mbjs_std_promise_rejection_tracker(MBJSContext *ctx,
                                                     MBJSValueConst promise,
                                                     MBJSValueConst reason,
                                                     bool is_handled,
                                                     void *opaque);
// Defaults to MBJS_NewRuntime, no-op if compiled without worker support.
// Call before creating the first worker thread.
MBJS_LIBC_EXTERN void mbjs_std_set_worker_new_runtime_func(MBJSRuntime *(*func)(void));
// Defaults to MBJS_NewContext, no-op if compiled without worker support.
// Call before creating the first worker thread.
MBJS_LIBC_EXTERN void mbjs_std_set_worker_new_context_func(MBJSContext *(*func)(MBJSRuntime *rt));

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* mbjs_LIBC_H */
