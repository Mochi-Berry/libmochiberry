/*
 * mbjs: Example of C module
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

#include <mbjs.h>

#define countof(x) (sizeof(x) / sizeof((x)[0]))

static int fib(int n)
{
    if (n <= 0)
        return 0;
    else if (n == 1)
        return 1;
    else
        return fib(n - 1) + fib(n - 2);
}

static MBJSValue mbjs_fib(MBJSContext *ctx, MBJSValue this_val,
                      int argc, MBJSValue *argv)
{
    int n, res;
    if (MBJS_ToInt32(ctx, &n, argv[0]))
        return MBJS_EXCEPTION;
    res = fib(n);
    return MBJS_NewInt32(ctx, res);
}

static const MBJSCFunctionListEntry mbjs_fib_funcs[] = {
    MBJS_CFUNC_DEF("fib", 1, mbjs_fib ),
};

static int mbjs_fib_init(MBJSContext *ctx, MBJSModuleDef *m)
{
    return MBJS_SetModuleExportList(ctx, m, mbjs_fib_funcs,
                                  countof(mbjs_fib_funcs));
}

MBJS_MODULE_EXTERN MBJSModuleDef *mbjs_init_module(MBJSContext *ctx, const char *module_name)
{
    MBJSModuleDef *m;
    m = MBJS_NewCModule(ctx, module_name, mbjs_fib_init);
    if (!m)
        return NULL;
    MBJS_AddModuleExportList(ctx, m, mbjs_fib_funcs, countof(mbjs_fib_funcs));
    return m;
}
