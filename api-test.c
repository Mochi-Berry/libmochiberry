#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbjs.h"
#include "mbjs-libc.h"
#include "cutils.h"

static MBJSRuntime *new_runtime(void)
{
    MBJSRuntime *rt = MBJS_NewRuntime();

    if (rt)
        MBJS_SetDumpFlags(rt, MBJS_ABORT_ON_LEAKS);
    return rt;
}

static MBJSValue eval(MBJSContext *ctx, const char *code)
{
    return MBJS_Eval(ctx, code, strlen(code), "<input>", MBJS_EVAL_TYPE_GLOBAL);
}

static MBJSValue cfunc_callback(MBJSContext *ctx, MBJSValueConst this_val,
                              int argc, MBJSValueConst *argv)
{
    return MBJS_ThrowTypeError(ctx, "from cfunc");
}

static MBJSValue cfuncdata_callback(MBJSContext *ctx, MBJSValueConst this_val,
                                  int argc, MBJSValueConst *argv,
                                  int magic, MBJSValueConst *func_data)
{
    return MBJS_ThrowTypeError(ctx, "from cfuncdata");
}

static MBJSValue cclosure_callback(MBJSContext *ctx, MBJSValueConst this_val,
                                 int argc, MBJSValueConst *argv,
                                 int magic, void *func_data)
{
  return MBJS_ThrowTypeError(ctx, "from cclosure");
}

static bool closure_finalized = false;

static void cclosure_opaque_finalize(void *opaque)
{
    if ((intptr_t)opaque == 12)
        closure_finalized = true;
}

static void cfunctions(void)
{
    uint32_t length;
    const char *s;
    MBJSValue ret, stack;

    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue cfunc = MBJS_NewCFunction(ctx, cfunc_callback, "cfunc", 42);
    MBJSValue cfuncdata =
        MBJS_NewCFunctionData2(ctx, cfuncdata_callback, "cfuncdata",
                             /*length*/1337, /*magic*/0, /*data_len*/0, NULL);
    MBJSValue cclosure =
        MBJS_NewCClosure(ctx, cclosure_callback, "cclosure", cclosure_opaque_finalize,
                       /*length*/0xC0DE, /*magic*/11, /*opaque*/(void*)12);
    MBJSValue global = MBJS_GetGlobalObject(ctx);
    MBJS_SetPropertyStr(ctx, global, "cfunc", cfunc);
    MBJS_SetPropertyStr(ctx, global, "cfuncdata", cfuncdata);
    MBJS_SetPropertyStr(ctx, global, "cclosure", cclosure);
    MBJS_FreeValue(ctx, global);

    ret = eval(ctx, "cfunc.name");
    assert(!MBJS_IsException(ret));
    assert(MBJS_IsString(ret));
    s = MBJS_ToCString(ctx, ret);
    MBJS_FreeValue(ctx, ret);
    assert(s);
    assert(!strcmp(s, "cfunc"));
    MBJS_FreeCString(ctx, s);
    ret = eval(ctx, "cfunc.length");
    assert(!MBJS_IsException(ret));
    assert(MBJS_IsNumber(ret));
    assert(0 == MBJS_ToUint32(ctx, &length, ret));
    assert(length == 42);

    ret = eval(ctx, "cfuncdata.name");
    assert(!MBJS_IsException(ret));
    assert(MBJS_IsString(ret));
    s = MBJS_ToCString(ctx, ret);
    MBJS_FreeValue(ctx, ret);
    assert(s);
    assert(!strcmp(s, "cfuncdata"));
    MBJS_FreeCString(ctx, s);
    ret = eval(ctx, "cfuncdata.length");
    assert(!MBJS_IsException(ret));
    assert(MBJS_IsNumber(ret));
    assert(0 == MBJS_ToUint32(ctx, &length, ret));
    assert(length == 1337);

    ret = eval(ctx, "cclosure.name");
    assert(!MBJS_IsException(ret));
    assert(MBJS_IsString(ret));
    s = MBJS_ToCString(ctx, ret);
    MBJS_FreeValue(ctx, ret);
    assert(s);
    assert(!strcmp(s, "cclosure"));
    MBJS_FreeCString(ctx, s);
    ret = eval(ctx, "cclosure.length");
    assert(!MBJS_IsException(ret));
    assert(MBJS_IsNumber(ret));
    assert(0 == MBJS_ToUint32(ctx, &length, ret));
    assert(length == 0xC0DE);

    ret = eval(ctx, "cfunc()");
    assert(MBJS_IsException(ret));
    ret = MBJS_GetException(ctx);
    assert(MBJS_IsError(ret));
    stack = MBJS_GetPropertyStr(ctx, ret, "stack");
    assert(MBJS_IsString(stack));
    s = MBJS_ToCString(ctx, stack);
    MBJS_FreeValue(ctx, stack);
    assert(s);
    assert(!strcmp(s, "    at cfunc (native)\n    at <eval> (<input>:1:1)\n"));
    MBJS_FreeCString(ctx, s);
    s = MBJS_ToCString(ctx, ret);
    MBJS_FreeValue(ctx, ret);
    assert(s);
    assert(!strcmp(s, "TypeError: from cfunc"));
    MBJS_FreeCString(ctx, s);

    ret = eval(ctx, "cfuncdata()");
    assert(MBJS_IsException(ret));
    ret = MBJS_GetException(ctx);
    assert(MBJS_IsError(ret));
    stack = MBJS_GetPropertyStr(ctx, ret, "stack");
    assert(MBJS_IsString(stack));
    s = MBJS_ToCString(ctx, stack);
    MBJS_FreeValue(ctx, stack);
    assert(s);
    assert(!strcmp(s, "    at cfuncdata (native)\n    at <eval> (<input>:1:1)\n"));
    MBJS_FreeCString(ctx, s);
    s = MBJS_ToCString(ctx, ret);
    MBJS_FreeValue(ctx, ret);
    assert(s);
    assert(!strcmp(s, "TypeError: from cfuncdata"));
    MBJS_FreeCString(ctx, s);

    ret = eval(ctx, "cclosure()");
    assert(MBJS_IsException(ret));
    ret = MBJS_GetException(ctx);
    assert(MBJS_IsError(ret));
    stack = MBJS_GetPropertyStr(ctx, ret, "stack");
    assert(MBJS_IsString(stack));
    s = MBJS_ToCString(ctx, stack);
    MBJS_FreeValue(ctx, stack);
    assert(s);
    assert(!strcmp(s, "    at cclosure (native)\n    at <eval> (<input>:1:1)\n"));
    MBJS_FreeCString(ctx, s);
    s = MBJS_ToCString(ctx, ret);
    MBJS_FreeValue(ctx, ret);
    assert(s);
    assert(!strcmp(s, "TypeError: from cclosure"));
    MBJS_FreeCString(ctx, s);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);

    assert(closure_finalized);
}

#define MAX_TIME 10

static int timeout_interrupt_handler(MBJSContext *ctx, void *opaque)
{
    int *time = (int *)opaque;
    return (*time)++ > MAX_TIME;
}

static void sync_call(void)
{
    static const char code[] =
"(function() { \
    try { \
        while (true) {} \
    } catch (e) {} \
})();";

    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    int time = 0;
    MBJS_SetInterruptHandler(rt, timeout_interrupt_handler, &time);
    MBJSValue ret = eval(ctx, code);
    assert(time > MAX_TIME);
    assert(MBJS_IsException(ret));
    MBJS_FreeValue(ctx, ret);
    assert(MBJS_HasException(ctx));
    MBJSValue e = MBJS_GetException(ctx);
    assert(MBJS_IsUncatchableError(e));
    MBJS_FreeValue(ctx, e);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void async_call(void)
{
    static const char code[] =
"(async function() { \
    const loop = async () => { \
        await Promise.resolve(); \
        while (true) {} \
    }; \
    await loop().catch(() => {}); \
})();";

    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    int time = 0;
    MBJS_SetInterruptHandler(rt, timeout_interrupt_handler, &time);
    MBJSValue ret = eval(ctx, code);
    assert(!MBJS_IsException(ret));
    MBJS_FreeValue(ctx, ret);
    assert(MBJS_IsJobPending(rt));
    int r = 0;
    while (MBJS_IsJobPending(rt)) {
        r = MBJS_ExecutePendingJob(rt, &ctx);
    }
    assert(time > MAX_TIME);
    assert(r == -1);
    assert(MBJS_HasException(ctx));
    MBJSValue e = MBJS_GetException(ctx);
    assert(MBJS_IsUncatchableError(e));
    MBJS_FreeValue(ctx, e);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void std_eval_interrupt_handler(void)
{
    static const char code[] =
        "import * as std from 'std'; std.evalScript('for(;;){}')";
    MBJSRuntime *rt = new_runtime();
    mbjs_std_init_handlers(rt);
    MBJSContext *ctx = MBJS_NewContext(rt);
    mbjs_init_module_std(ctx, "std");
    int time = 0;
    MBJS_SetInterruptHandler(rt, timeout_interrupt_handler, &time);
    MBJSValue ret =
        MBJS_Eval(ctx, code, strlen(code), "<input>", MBJS_EVAL_TYPE_MODULE);
    ret = mbjs_std_await(ctx, ret);
    assert(time > MAX_TIME);
    assert(MBJS_IsException(ret));
    ret = MBJS_GetException(ctx);
    assert(MBJS_IsError(ret));
    // uncatchable "interrupted" exception is turned into a regular exception
    assert(!MBJS_IsUncatchableError(ret));
    const char *str = MBJS_ToCString(ctx, ret);
    assert(str != NULL);
    assert(strstr(str, "InternalError: interrupted"));
    MBJS_FreeCString(ctx, str);
    MBJS_FreeValue(ctx, ret);
    uintptr_t interrupt_handler = mbjs_std_cmd(/*GetInterruptHandler*/5, rt);
    uintptr_t interrupt_opaque = mbjs_std_cmd(/*GetInterruptOpaque*/6, rt);
    assert(interrupt_handler == (uintptr_t)timeout_interrupt_handler);
    assert(interrupt_opaque == (uintptr_t)&time);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static MBJSValue save_value(MBJSContext *ctx, MBJSValueConst this_val,
                          int argc, MBJSValueConst *argv)
{
    assert(argc == 1);
    MBJSValue *p = (MBJSValue *)MBJS_GetContextOpaque(ctx);
    *p = MBJS_DupValue(ctx, argv[0]);
    return MBJS_UNDEFINED;
}

static void async_call_stack_overflow(void)
{
    static const char code[] =
"(async function() { \
    const f = () => f(); \
    try { \
        await Promise.resolve(); \
        f(); \
    } catch (e) { \
        save_value(e); \
    } \
})();";

    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue value = MBJS_UNDEFINED;
    MBJS_SetContextOpaque(ctx, &value);
    MBJSValue global = MBJS_GetGlobalObject(ctx);
    MBJS_SetPropertyStr(ctx, global, "save_value", MBJS_NewCFunction(ctx, save_value, "save_value", 1));
    MBJS_FreeValue(ctx, global);
    MBJSValue ret = eval(ctx, code);
    assert(!MBJS_IsException(ret));
    MBJS_FreeValue(ctx, ret);
    assert(MBJS_IsJobPending(rt));
    int r = 0;
    while (MBJS_IsJobPending(rt)) {
        r = MBJS_ExecutePendingJob(rt, &ctx);
    }
    assert(r == 1);
    assert(!MBJS_HasException(ctx));
    assert(MBJS_IsError(value)); // stack overflow should be caught
    MBJS_FreeValue(ctx, value);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

// https://github.com/mbjs-ng/mbjs/issues/914
static void raw_context_global_var(void)
{
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContextRaw(rt);
    MBJS_AddIntrinsicEval(ctx);
    {
        MBJSValue ret = eval(ctx, "globalThis");
        assert(MBJS_IsException(ret));
        MBJS_FreeValue(ctx, ret);
    }
    {
        MBJSValue ret = eval(ctx, "var x = 42");
        assert(MBJS_IsUndefined(ret));
        MBJS_FreeValue(ctx, ret);
    }
    {
        MBJSValue ret = eval(ctx, "function f() {}");
        assert(MBJS_IsUndefined(ret));
        MBJS_FreeValue(ctx, ret);
    }
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void is_array(void)
{
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    {
        MBJSValue ret = eval(ctx, "[]");
        assert(!MBJS_IsException(ret));
        assert(MBJS_IsArray(ret));
        MBJS_FreeValue(ctx, ret);
    }
    {
        MBJSValue ret = eval(ctx, "new Proxy([], {})");
        assert(!MBJS_IsException(ret));
        assert(!MBJS_IsArray(ret));
        assert(MBJS_IsProxy(ret));
        MBJSValue handler = MBJS_GetProxyHandler(ctx, ret);
        MBJSValue target = MBJS_GetProxyTarget(ctx, ret);
        assert(!MBJS_IsException(handler));
        assert(!MBJS_IsException(target));
        assert(!MBJS_IsProxy(handler));
        assert(!MBJS_IsProxy(target));
        assert(MBJS_IsObject(handler));
        assert(MBJS_IsArray(target));
        MBJS_FreeValue(ctx, handler);
        MBJS_FreeValue(ctx, target);
        MBJS_FreeValue(ctx, ret);
    }
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static int loader_calls;

static MBJSModuleDef *loader(MBJSContext *ctx, const char *name, void *opaque)
{
    loader_calls++;
    assert(!strcmp(name, "b"));
    static const char code[] = "export function f(x){}";
    MBJSValue ret = MBJS_Eval(ctx, code, strlen(code), "b",
                          MBJS_EVAL_TYPE_MODULE|MBJS_EVAL_FLAG_COMPILE_ONLY);
    assert(!MBJS_IsException(ret));
    MBJSModuleDef *m = MBJS_VALUE_GET_PTR(ret);
    assert(m);
    MBJS_FreeValue(ctx, ret);
    return m;
}

static void module_serde(void)
{
    MBJSRuntime *rt = new_runtime();
    //MBJS_SetDumpFlags(rt, MBJS_DUMP_MODULE_RESOLVE);
    MBJS_SetModuleLoaderFunc(rt, NULL, loader, NULL);
    MBJSContext *ctx = MBJS_NewContext(rt);
    static const char code[] = "import {f} from 'b'; f()";
    assert(loader_calls == 0);
    MBJSValue mod = MBJS_Eval(ctx, code, strlen(code), "a",
                          MBJS_EVAL_TYPE_MODULE|MBJS_EVAL_FLAG_COMPILE_ONLY);
    assert(loader_calls == 1);
    assert(!MBJS_IsException(mod));
    assert(MBJS_IsModule(mod));
    size_t len = 0;
    uint8_t *buf = MBJS_WriteObject(ctx, &len, mod,
                                  MBJS_WRITE_OBJ_BYTECODE|MBJS_WRITE_OBJ_REFERENCE);
    assert(buf);
    assert(len > 0);
    MBJS_FreeValue(ctx, mod);
    assert(loader_calls == 1);
    mod = MBJS_ReadObject(ctx, buf, len, MBJS_READ_OBJ_BYTECODE);
    mbjs_free(ctx, buf);
    assert(loader_calls == 1); // 'b' is returned from cache
    assert(!MBJS_IsException(mod));
    MBJSValue ret = MBJS_EvalFunction(ctx, mod);
    assert(!MBJS_IsException(ret));
    assert(MBJS_IsPromise(ret));
    MBJSValue result = MBJS_PromiseResult(ctx, ret);
    assert(!MBJS_IsException(result));
    assert(MBJS_IsUndefined(result));
    MBJS_FreeValue(ctx, result);
    MBJS_FreeValue(ctx, ret);
    MBJS_FreeValue(ctx, mod);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

struct rejection_counts {
    int reject_count;
    int handle_count;
};

static void rejection_counter(MBJSContext *ctx, MBJSValueConst promise,
                              MBJSValueConst reason, bool is_handled, void *opaque)
{
    struct rejection_counts *c = opaque;
    if (is_handled)
        c->handle_count++;
    else
        c->reject_count++;
}

// A synchronous module that throws at top level must surface exactly one unhandled rejection
static void module_unhandled_rejection(void)
{
    struct rejection_counts c = {0, 0};
    MBJSRuntime *rt = new_runtime();
    MBJS_SetHostPromiseRejectionTracker(rt, rejection_counter, &c);
    MBJSContext *ctx = MBJS_NewContext(rt);

    static const char code[] = "throw new Error('Nuke the entire site from orbit. It\\'s the only way to be sure.')";
    MBJSValue v = MBJS_Eval(ctx, code, strlen(code), "<m>", MBJS_EVAL_TYPE_MODULE);
    MBJS_FreeValue(ctx, v);

    MBJSContext *c1;
    while (MBJS_ExecutePendingJob(rt, &c1) > 0)
        ;

    // net unhandled rejections == (2 rejects - 1 handled)
    assert(c.reject_count == 2);
    assert(c.handle_count == 1);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void promise_mark_as_handled(void)
{
    struct rejection_counts c = {0, 0};
    MBJSRuntime *rt = new_runtime();
    MBJS_SetHostPromiseRejectionTracker(rt, rejection_counter, &c);
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSContext *c1;

    // marking an already-rejected promise notifies the tracker exactly once
    static const char code[] = "Promise.reject('kaboom')";
    MBJSValue promise = MBJS_Eval(ctx, code, strlen(code), "<t>", MBJS_EVAL_TYPE_GLOBAL);
    assert(MBJS_IsPromise(promise));
    while (MBJS_ExecutePendingJob(rt, &c1) > 0)
        ;
    assert(c.reject_count == 1);
    assert(c.handle_count == 0);
    MBJS_PromiseMarkAsHandled(ctx, promise);
    assert(c.handle_count == 1);
    MBJS_PromiseMarkAsHandled(ctx, promise);
    assert(c.handle_count == 1);
    MBJS_FreeValue(ctx, promise);

    // marking a pending promise suppresses the report when it later rejects
    MBJSValue resolving_funcs[2];
    MBJSValue promise2 = MBJS_NewPromiseCapability(ctx, resolving_funcs);
    MBJS_PromiseMarkAsHandled(ctx, promise2);
    MBJSValue reason = MBJS_NewString(ctx, "unseen");
    MBJSValue ret = MBJS_Call(ctx, resolving_funcs[1], MBJS_UNDEFINED, 1,
                          (MBJSValueConst *)&reason);
    while (MBJS_ExecutePendingJob(rt, &c1) > 0)
        ;
    assert(c.reject_count == 1);
    assert(c.handle_count == 1);
    MBJS_FreeValue(ctx, ret);
    MBJS_FreeValue(ctx, reason);
    MBJS_FreeValue(ctx, resolving_funcs[0]);
    MBJS_FreeValue(ctx, resolving_funcs[1]);
    MBJS_FreeValue(ctx, promise2);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void promise_then(void)
{
    const char *s;
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSContext *c1;
    MBJSValue got = MBJS_UNDEFINED;
    MBJS_SetContextOpaque(ctx, &got);

    // the result promise is intrinsic: neither an overridden then() nor
    // Symbol.species is consulted
    static const char code[] =
        "class P extends Promise {"
        "  static get [Symbol.species]() { throw new Error('species'); }"
        "  then() { throw new Error('then'); }"
        "}"
        "P.resolve('ok')";
    MBJSValue promise = eval(ctx, code);
    assert(MBJS_IsPromise(promise));
    MBJSValue on_fulfilled = MBJS_NewCFunction(ctx, save_value, "onFulfilled", 1);
    MBJSValue result = MBJS_PromiseThen(ctx, promise, on_fulfilled, MBJS_UNDEFINED);
    assert(MBJS_IsPromise(result));
    while (MBJS_ExecutePendingJob(rt, &c1) > 0)
        ;
    s = MBJS_ToCString(ctx, got);
    assert(s);
    assert(!strcmp(s, "ok"));
    MBJS_FreeCString(ctx, s);
    MBJS_FreeValue(ctx, got);
    assert(MBJS_PromiseState(ctx, result) == MBJS_PROMISE_FULFILLED);
    MBJS_FreeValue(ctx, result);
    MBJS_FreeValue(ctx, on_fulfilled);
    MBJS_FreeValue(ctx, promise);

    // rejection handler receives the reason when the promise rejects later
    MBJSValue resolving_funcs[2];
    MBJSValue promise2 = MBJS_NewPromiseCapability(ctx, resolving_funcs);
    MBJSValue on_rejected = MBJS_NewCFunction(ctx, save_value, "onRejected", 1);
    MBJSValue result2 = MBJS_PromiseThen(ctx, promise2, MBJS_UNDEFINED, on_rejected);
    assert(MBJS_IsPromise(result2));
    got = MBJS_UNDEFINED;
    MBJSValue reason = MBJS_NewString(ctx, "boom");
    MBJSValue ret = MBJS_Call(ctx, resolving_funcs[1], MBJS_UNDEFINED, 1,
                          (MBJSValueConst *)&reason);
    while (MBJS_ExecutePendingJob(rt, &c1) > 0)
        ;
    s = MBJS_ToCString(ctx, got);
    assert(s);
    assert(!strcmp(s, "boom"));
    MBJS_FreeCString(ctx, s);
    MBJS_FreeValue(ctx, got);
    assert(MBJS_PromiseState(ctx, result2) == MBJS_PROMISE_FULFILLED);
    MBJS_FreeValue(ctx, ret);
    MBJS_FreeValue(ctx, reason);
    MBJS_FreeValue(ctx, result2);
    MBJS_FreeValue(ctx, on_rejected);
    MBJS_FreeValue(ctx, resolving_funcs[0]);
    MBJS_FreeValue(ctx, resolving_funcs[1]);
    MBJS_FreeValue(ctx, promise2);

    // a non-promise argument is a TypeError
    MBJSValue bad = MBJS_PromiseThen(ctx, MBJS_UNDEFINED, MBJS_UNDEFINED, MBJS_UNDEFINED);
    assert(MBJS_IsException(bad));
    MBJSValue exc = MBJS_GetException(ctx);
    MBJS_FreeValue(ctx, exc);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void runtime_cstring_free(void)
{
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    // string -> cstring + MBJS_FreeCStringRT
    {
        MBJSValue ret = eval(ctx, "\"testStringPleaseIgnore\"");
        assert(MBJS_IsString(ret));
        const char *s = MBJS_ToCString(ctx, ret);
        assert(s);
        assert(strcmp(s, "testStringPleaseIgnore") == 0);
        MBJS_FreeCStringRT(rt, s);
        MBJS_FreeValue(ctx, ret);
    }
    // string -> cstring + MBJS_FreeCStringRT, destroying the source value first
    {
        MBJSValue ret = eval(ctx, "\"testStringPleaseIgnore\"");
        assert(MBJS_IsString(ret));
        const char *s = MBJS_ToCString(ctx, ret);
        assert(s);
        MBJS_FreeValue(ctx, ret);
        assert(strcmp(s, "testStringPleaseIgnore") == 0);
        MBJS_FreeCStringRT(rt, s);
    }
    // number -> cstring + MBJS_FreeCStringRT
    {
        MBJSValue ret = eval(ctx, "123987");
        assert(MBJS_IsNumber(ret));
        const char *s = MBJS_ToCString(ctx, ret);
        assert(s);
        assert(strcmp(s, "123987") == 0);
        MBJS_FreeCStringRT(rt, s);
        MBJS_FreeValue(ctx, ret);
    }
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void utf16_string(void)
{
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    {
        MBJSValue v = MBJS_NewStringUTF16(ctx, NULL, 0);
        assert(!MBJS_IsException(v));
        const char *s = MBJS_ToCString(ctx, v);
        assert(s);
        assert(!strcmp(s, ""));
        MBJS_FreeCString(ctx, s);
        MBJS_FreeValue(ctx, v);
    }
    {
        MBJSValue v = MBJS_NewStringUTF16(ctx, (uint16_t[]){'o','k'}, 2);
        assert(!MBJS_IsException(v));
        const char *s = MBJS_ToCString(ctx, v);
        assert(s);
        assert(!strcmp(s, "ok"));
        MBJS_FreeCString(ctx, s);
        size_t n;
        const uint16_t *u = MBJS_ToCStringLenUTF16(ctx, &n, v);
        assert(u);
        assert(n == 2);
        assert(u[0] == 'o');
        assert(u[1] == 'k');
        MBJS_FreeCStringUTF16(ctx, u);
        MBJS_FreeValue(ctx, v);
    }
    {
        MBJSValue v = MBJS_NewStringUTF16(ctx, (uint16_t[]){0xD800}, 1);
        assert(!MBJS_IsException(v));
        const char *s = MBJS_ToCString(ctx, v);
        assert(s);
        // questionable but surrogates don't map to UTF-8 without WTF-8
        assert(!strcmp(s, "\xED\xA0\x80"));
        MBJS_FreeCString(ctx, s);
        size_t n;
        const uint16_t *u = MBJS_ToCStringLenUTF16(ctx, &n, v);
        assert(u);
        assert(n == 1);
        assert(u[0] == 0xD800);
        MBJS_FreeCStringUTF16(ctx, u);
        MBJS_FreeValue(ctx, v);
    }
    {
        MBJSValue v = MBJS_NewStringLen(ctx, "ok", 2); // ascii -> ucs
        assert(!MBJS_IsException(v));
        size_t n;
        const uint16_t *u = MBJS_ToCStringLenUTF16(ctx, &n, v);
        assert(u);
        assert(n == 2);
        assert(u[0] == 'o');
        assert(u[1] == 'k');
        MBJS_FreeCStringUTF16(ctx, u);
        MBJS_FreeValue(ctx, v);
    }
    {
        MBJSValue v = MBJS_NewStringUTF16(ctx, NULL, (size_t)INT_MAX + 1);
        assert(MBJS_IsException(v));
        MBJSValue e = MBJS_GetException(ctx);
        assert(MBJS_IsError(e));
        const char *s = MBJS_ToCString(ctx, e);
        assert(s);
        assert(strstr(s, "invalid string length") != NULL);
        MBJS_FreeCString(ctx, s);
        MBJS_FreeValue(ctx, e);
    }
    {
        // create wide char slice string, see MBJS_STRING_SLICE_LEN_MAX
        MBJSValue v = eval(ctx, "`\\uD800\\uDC00`.repeat(8192).slice(0, -1)");
        assert(!MBJS_IsException(v));
        size_t n;
        const uint16_t *u = MBJS_ToCStringLenUTF16(ctx, &n, v);
        assert(u);
        assert(n == 2*8192-1);
        assert(u[0] == 0xD800);
        assert(u[1] == 0xDC00);
        MBJS_FreeCStringUTF16(ctx, u);
        MBJS_FreeValue(ctx, v);
    }
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void weak_map_gc_check(void)
{
    static const char init_code[] =
"const map = new WeakMap(); \
function addItem() { \
    const k = { \
        text: 'a', \
    }; \
    map.set(k, {k}); \
}";
    static const char test_code[] = "addItem()";

    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);

    MBJSValue ret = eval(ctx, init_code);
    assert(!MBJS_IsException(ret));

    MBJSValue ret_test = eval(ctx, test_code);
    assert(!MBJS_IsException(ret_test));
    MBJS_RunGC(rt);
    MBJSMemoryUsage memory_usage;
    MBJS_ComputeMemoryUsage(rt, &memory_usage);

    for (int i = 0; i < 3; i++) {
        MBJSValue ret_test2 = eval(ctx, test_code);
        assert(!MBJS_IsException(ret_test2));
        MBJS_RunGC(rt);
        MBJSMemoryUsage memory_usage2;
        MBJS_ComputeMemoryUsage(rt, &memory_usage2);

        assert(memory_usage.memory_used_count == memory_usage2.memory_used_count);
        assert(memory_usage.memory_used_size == memory_usage2.memory_used_size);
        MBJS_FreeValue(ctx, ret_test2);
    }

    MBJS_FreeValue(ctx, ret);
    MBJS_FreeValue(ctx, ret_test);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

struct {
    int hook_type_call_count[4];
} promise_hook_state;

static void promise_hook_cb(MBJSContext *ctx, MBJSPromiseHookType type,
                            MBJSValueConst promise, MBJSValueConst parent_promise,
                            void *opaque)
{
    assert(type == MBJS_PROMISE_HOOK_INIT ||
           type == MBJS_PROMISE_HOOK_BEFORE ||
           type == MBJS_PROMISE_HOOK_AFTER ||
           type == MBJS_PROMISE_HOOK_RESOLVE);
    promise_hook_state.hook_type_call_count[type]++;
    assert(opaque == (void *)&promise_hook_state);
    if (!MBJS_IsUndefined(parent_promise)) {
        MBJSValue global_object = MBJS_GetGlobalObject(ctx);
        MBJS_SetPropertyStr(ctx, global_object, "actual",
                          MBJS_DupValue(ctx, parent_promise));
        MBJS_FreeValue(ctx, global_object);
    }
}

static void promise_hook(void)
{
    int *cc = promise_hook_state.hook_type_call_count;
    MBJSContext *unused;
    MBJSRuntime *rt = new_runtime();
    //MBJS_SetDumpFlags(rt, MBJS_DUMP_PROMISE);
    MBJS_SetPromiseHook(rt, promise_hook_cb, &promise_hook_state);
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue global_object = MBJS_GetGlobalObject(ctx);
    {
        // empty module; creates an outer and inner module promise;
        // MBJS_Eval returns the outer promise
        MBJSValue ret = MBJS_Eval(ctx, "", 0, "<input>", MBJS_EVAL_TYPE_MODULE);
        assert(!MBJS_IsException(ret));
        assert(MBJS_IsPromise(ret));
        assert(MBJS_PROMISE_FULFILLED == MBJS_PromiseState(ctx, ret));
        MBJS_FreeValue(ctx, ret);
        assert(2 == cc[MBJS_PROMISE_HOOK_INIT]);
        assert(0 == cc[MBJS_PROMISE_HOOK_BEFORE]);
        assert(0 == cc[MBJS_PROMISE_HOOK_AFTER]);
        assert(2 == cc[MBJS_PROMISE_HOOK_RESOLVE]);
        assert(!MBJS_IsJobPending(rt));
    }
    memset(&promise_hook_state, 0, sizeof(promise_hook_state));
    {
        // module with unresolved promise; the outer and inner module promises
        // are resolved but not the user's promise
        static const char code[] = "new Promise(() => {})";
        MBJSValue ret = MBJS_Eval(ctx, code, strlen(code), "<input>", MBJS_EVAL_TYPE_MODULE);
        assert(!MBJS_IsException(ret));
        assert(MBJS_IsPromise(ret));
        assert(MBJS_PROMISE_FULFILLED == MBJS_PromiseState(ctx, ret)); // outer module promise
        MBJS_FreeValue(ctx, ret);
        assert(3 == cc[MBJS_PROMISE_HOOK_INIT]);
        assert(0 == cc[MBJS_PROMISE_HOOK_BEFORE]);
        assert(0 == cc[MBJS_PROMISE_HOOK_AFTER]);
        assert(2 == cc[MBJS_PROMISE_HOOK_RESOLVE]); // outer and inner module promise
        assert(!MBJS_IsJobPending(rt));
    }
    memset(&promise_hook_state, 0, sizeof(promise_hook_state));
    {
        // module with resolved promise
        static const char code[] = "new Promise((resolve,reject) => resolve())";
        MBJSValue ret = MBJS_Eval(ctx, code, strlen(code), "<input>", MBJS_EVAL_TYPE_MODULE);
        assert(!MBJS_IsException(ret));
        assert(MBJS_IsPromise(ret));
        assert(MBJS_PROMISE_FULFILLED == MBJS_PromiseState(ctx, ret)); // outer module promise
        MBJS_FreeValue(ctx, ret);
        assert(3 == cc[MBJS_PROMISE_HOOK_INIT]);
        assert(0 == cc[MBJS_PROMISE_HOOK_BEFORE]);
        assert(0 == cc[MBJS_PROMISE_HOOK_AFTER]);
        assert(3 == cc[MBJS_PROMISE_HOOK_RESOLVE]);
        assert(!MBJS_IsJobPending(rt));
    }
    memset(&promise_hook_state, 0, sizeof(promise_hook_state));
    {
        // module with rejected promise
        static const char code[] = "new Promise((resolve,reject) => reject())";
        MBJSValue ret = MBJS_Eval(ctx, code, strlen(code), "<input>", MBJS_EVAL_TYPE_MODULE);
        assert(!MBJS_IsException(ret));
        assert(MBJS_IsPromise(ret));
        assert(MBJS_PROMISE_FULFILLED == MBJS_PromiseState(ctx, ret)); // outer module promise
        MBJS_FreeValue(ctx, ret);
        assert(3 == cc[MBJS_PROMISE_HOOK_INIT]);
        assert(0 == cc[MBJS_PROMISE_HOOK_BEFORE]);
        assert(0 == cc[MBJS_PROMISE_HOOK_AFTER]);
        assert(2 == cc[MBJS_PROMISE_HOOK_RESOLVE]);
        assert(!MBJS_IsJobPending(rt));
    }
    memset(&promise_hook_state, 0, sizeof(promise_hook_state));
    {
        // module with promise chain
        static const char code[] =
            "globalThis.count = 0;"
            "globalThis.actual = undefined;" // set by promise_hook_cb
            "globalThis.expected = new Promise(resolve => resolve());"
            "expected.then(_ => count++)";
        MBJSValue ret = MBJS_Eval(ctx, code, strlen(code), "<input>", MBJS_EVAL_TYPE_MODULE);
        assert(!MBJS_IsException(ret));
        assert(MBJS_IsPromise(ret));
        assert(MBJS_PROMISE_FULFILLED == MBJS_PromiseState(ctx, ret)); // outer module promise
        MBJS_FreeValue(ctx, ret);
        assert(4 == cc[MBJS_PROMISE_HOOK_INIT]);
        assert(0 == cc[MBJS_PROMISE_HOOK_BEFORE]);
        assert(0 == cc[MBJS_PROMISE_HOOK_AFTER]);
        assert(3 == cc[MBJS_PROMISE_HOOK_RESOLVE]);
        MBJSValue v = MBJS_GetPropertyStr(ctx, global_object, "count");
        assert(!MBJS_IsException(v));
        int32_t count;
        assert(0 == MBJS_ToInt32(ctx, &count, v));
        assert(0 == count);
        MBJS_FreeValue(ctx, v);
        assert(MBJS_IsJobPending(rt));
        assert(1 == MBJS_ExecutePendingJob(rt, &unused));
        assert(!MBJS_HasException(ctx));
        assert(4 == cc[MBJS_PROMISE_HOOK_INIT]);
        assert(0 == cc[MBJS_PROMISE_HOOK_BEFORE]);
        assert(0 == cc[MBJS_PROMISE_HOOK_AFTER]);
        assert(4 == cc[MBJS_PROMISE_HOOK_RESOLVE]);
        assert(!MBJS_IsJobPending(rt));
        v = MBJS_GetPropertyStr(ctx, global_object, "count");
        assert(!MBJS_IsException(v));
        assert(0 == MBJS_ToInt32(ctx, &count, v));
        assert(1 == count);
        MBJS_FreeValue(ctx, v);
        MBJSValue actual = MBJS_GetPropertyStr(ctx, global_object, "actual");
        MBJSValue expected = MBJS_GetPropertyStr(ctx, global_object, "expected");
        assert(!MBJS_IsException(actual));
        assert(!MBJS_IsException(expected));
        assert(MBJS_IsSameValue(ctx, actual, expected));
        MBJS_FreeValue(ctx, actual);
        MBJS_FreeValue(ctx, expected);
    }
    memset(&promise_hook_state, 0, sizeof(promise_hook_state));
    {
        // module with thenable; fires before and after hooks
        static const char code[] =
            "new Promise(resolve => resolve({then(resolve){ resolve() }}))";
        MBJSValue ret = MBJS_Eval(ctx, code, strlen(code), "<input>", MBJS_EVAL_TYPE_MODULE);
        assert(!MBJS_IsException(ret));
        assert(MBJS_IsPromise(ret));
        assert(MBJS_PROMISE_FULFILLED == MBJS_PromiseState(ctx, ret)); // outer module promise
        MBJS_FreeValue(ctx, ret);
        assert(3 == cc[MBJS_PROMISE_HOOK_INIT]);
        assert(0 == cc[MBJS_PROMISE_HOOK_BEFORE]);
        assert(0 == cc[MBJS_PROMISE_HOOK_AFTER]);
        assert(2 == cc[MBJS_PROMISE_HOOK_RESOLVE]);
        assert(MBJS_IsJobPending(rt));
        assert(1 == MBJS_ExecutePendingJob(rt, &unused));
        assert(!MBJS_HasException(ctx));
        assert(3 == cc[MBJS_PROMISE_HOOK_INIT]);
        assert(1 == cc[MBJS_PROMISE_HOOK_BEFORE]);
        assert(1 == cc[MBJS_PROMISE_HOOK_AFTER]);
        assert(3 == cc[MBJS_PROMISE_HOOK_RESOLVE]);
        assert(!MBJS_IsJobPending(rt));
    }
    MBJS_FreeValue(ctx, global_object);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void dump_memory_usage(void)
{
    MBJSMemoryUsage stats;

    MBJSRuntime *rt = NULL;
    MBJSContext *ctx = NULL;

    rt = new_runtime();
    ctx = MBJS_NewContext(rt);

    //MBJS_SetDumpFlags(rt, MBJS_DUMP_PROMISE);

    static const char code[] =
    "globalThis.count = 0;"
    "globalThis.actual = undefined;" // set by promise_hook_cb
    "globalThis.expected = new Promise(resolve => resolve());"
    "expected.then(_ => count++)";

    MBJSValue evalVal = MBJS_Eval(ctx, code, strlen(code), "<input>", 0);
    MBJS_FreeValue(ctx, evalVal);

    FILE *temp = tmpfile();
    assert(temp != NULL);
    MBJS_ComputeMemoryUsage(rt, &stats);
    MBJS_DumpMemoryUsage(temp, &stats, rt);
    // MBJS_DumpMemoryUsage(stdout, &stats, rt);
    fclose(temp);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void new_errors(void)
{
    typedef struct {
        const char name[16];
        MBJSValue (*func)(MBJSContext *, const char *, ...);
    } Entry;
    static const Entry entries[] = {
        {"Error",           MBJS_NewPlainError},
        {"InternalError",   MBJS_NewInternalError},
        {"RangeError",      MBJS_NewRangeError},
        {"ReferenceError",  MBJS_NewReferenceError},
        {"SyntaxError",     MBJS_NewSyntaxError},
        {"TypeError",       MBJS_NewTypeError},
    };
    const Entry *e;

    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    for (e = entries; e < endof(entries); e++) {
        MBJSValue obj = (*e->func)(ctx, "the %s", "needle");
        assert(!MBJS_IsException(obj));
        assert(MBJS_IsObject(obj));
        assert(MBJS_IsError(obj));
        const char *haystack = MBJS_ToCString(ctx, obj);
        char needle[256];
        snprintf(needle, sizeof(needle), "%s: the needle", e->name);
        assert(strstr(haystack, needle));
        MBJS_FreeCString(ctx, haystack);
        MBJSValue stack = MBJS_GetPropertyStr(ctx, obj, "stack");
        assert(!MBJS_IsException(stack));
        assert(MBJS_IsString(stack));
        MBJS_FreeValue(ctx, stack);
        MBJS_FreeValue(ctx, obj);
    }
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

// MBJS_NewContext() already installs DOMException by way of
// MBJS_AddIntrinsicAToB(), so a host that also calls MBJS_AddIntrinsicDOMException()
// explicitly installs it twice. The second install must release the prototype
// the first one left in ctx->class_proto[] instead of overwriting the slot;
// new_runtime() aborts at MBJS_FreeRuntime() if it does not.
static void dom_exception_added_twice(void)
{
    MBJSValue ret;
    const char *s;
    int i;

    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);

    // the implicit install left a working DOMException behind
    ret = eval(ctx, "DOMException.prototype === "
                    "Object.getPrototypeOf(new DOMException)");
    assert(MBJS_ToBool(ctx, ret) == true);
    MBJS_FreeValue(ctx, ret);

    // installing it again, repeatedly, must not leak the previous prototype
    for (i = 0; i < 3; i++)
        assert(MBJS_AddIntrinsicDOMException(ctx) == 0);

    // and the class prototype still matches the global constructor, i.e. the
    // slot tracks the newest install rather than a freed or stale object
    ret = eval(ctx, "DOMException.prototype === "
                    "Object.getPrototypeOf(new DOMException)");
    assert(MBJS_ToBool(ctx, ret) == true);
    MBJS_FreeValue(ctx, ret);

    ret = eval(ctx, "new DOMException('m', 'InvalidCharacterError').name");
    assert(!MBJS_IsException(ret));
    s = MBJS_ToCString(ctx, ret);
    assert(s);
    assert(!strcmp(s, "InvalidCharacterError"));
    MBJS_FreeCString(ctx, s);
    MBJS_FreeValue(ctx, ret);

    // the internally thrown DOMExceptions pick up the current prototype too
    ret = eval(ctx, "try { atob('a') } catch (e) { "
                    "  e instanceof DOMException && e.name } ");
    assert(!MBJS_IsException(ret));
    s = MBJS_ToCString(ctx, ret);
    assert(s);
    assert(!strcmp(s, "InvalidCharacterError"));
    MBJS_FreeCString(ctx, s);
    MBJS_FreeValue(ctx, ret);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void backtrace_oom_callsite_array(void)
{
    static const char setup_code[] =
        "Error.prepareStackTrace = (e, frames) => frames;\n"
        "globalThis.f = () => new Error();\n";
    MBJSValue global_object, func, ret;
    MBJSMemoryUsage stats;
    uint32_t headroom;
    MBJSRuntime *rt;
    MBJSContext *ctx;

    rt = new_runtime();
    ctx = MBJS_NewContext(rt);
    global_object = MBJS_GetGlobalObject(ctx);

    ret = eval(ctx, setup_code);
    assert(!MBJS_IsException(ret));
    MBJS_FreeValue(ctx, ret);

    func = MBJS_GetPropertyStr(ctx, global_object, "f");
    assert(MBJS_IsFunction(ctx, func));

    for (headroom = 0; headroom < 2048; headroom++) {
        MBJS_ComputeMemoryUsage(rt, &stats);
        MBJS_SetMemoryLimit(rt, (size_t)stats.malloc_size + headroom);
        ret = MBJS_Call(ctx, func, MBJS_UNDEFINED, 0, NULL);
        MBJS_SetMemoryLimit(rt, 0);
        MBJS_FreeValue(ctx, ret);
        MBJS_FreeValue(ctx, MBJS_GetException(ctx));
    }

    MBJS_FreeValue(ctx, func);
    MBJS_FreeValue(ctx, global_object);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void large_allocation_accounting(void)
{
    static const size_t block_size = 1024 * 1024;
    MBJSMemoryUsage before, after;
    MBJSValue ret, exception;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    const char *str;

    rt = new_runtime();
    ctx = MBJS_NewContext(rt);

    MBJS_ComputeMemoryUsage(rt, &before);
    ret = eval(ctx, "globalThis.a = new Uint8Array(1024 * 1024)");
    assert(!MBJS_IsException(ret));
    MBJS_FreeValue(ctx, ret);
    MBJS_ComputeMemoryUsage(rt, &after);
    assert(after.malloc_size - before.malloc_size >= (int64_t)block_size);

    MBJS_ComputeMemoryUsage(rt, &before);
    MBJS_SetMemoryLimit(rt, (size_t)before.malloc_size + 4 * block_size);
    ret = eval(ctx, "globalThis.a = [];\n"
                    "for (let i = 0; i < 64; i++) {\n"
                    "    // OOM exception should be uncatchable\n"
                    "    try {\n"
                    "        a.push(new Uint8Array(1024 * 1024));"
                    "    } catch (e) {\n"
                    "        throw 'unexpected exception';\n"
                    "    }\n"
                    "}");
    assert(MBJS_IsException(ret));
    MBJS_SetMemoryLimit(rt, 0);
    MBJS_ComputeMemoryUsage(rt, &after);
    assert(after.malloc_size - before.malloc_size < (int64_t)(8 * block_size));

    exception = MBJS_GetException(ctx);
    assert(MBJS_IsError(exception));
    str = MBJS_ToCString(ctx, exception);
    assert(str);
    assert(!strcmp(str, "InternalError: out of memory"));
    MBJS_FreeCString(ctx, str);
    MBJS_FreeValue(ctx, exception);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void backtrace_oom_current_exception(void)
{
    static const char setup_code[] =
        "globalThis.f = function() { missing; };\n"
        "Object.defineProperty(f, 'name', { value: 'x'.repeat(2 * 1024 * 1024) });";
    MBJSMemoryUsage stats;
    MBJSValue ret, exception;
    MBJSRuntime *rt;
    MBJSContext *ctx;

    rt = new_runtime();
    ctx = MBJS_NewContext(rt);

    ret = eval(ctx, setup_code);
    assert(!MBJS_IsException(ret));
    MBJS_FreeValue(ctx, ret);

    MBJS_ComputeMemoryUsage(rt, &stats);
    MBJS_SetMemoryLimit(rt, (size_t)stats.malloc_size + 128 * 1024);

    ret = eval(ctx, "f()");
    assert(MBJS_IsException(ret));
    assert(MBJS_HasException(ctx));
    exception = MBJS_GetException(ctx);
    MBJS_FreeValue(ctx, exception);
    MBJS_SetMemoryLimit(rt, 0);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void proxy_own_keys_huge_length(void)
{
    static const char setup_code[] =
        "globalThis.p = new Proxy({}, {\n"
        "    ownKeys() { return { length: 0x20000000 }; },\n"
        "});";
    MBJSMemoryUsage stats;
    MBJSValue ret, exception;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    const char *str;

    rt = new_runtime();
    ctx = MBJS_NewContext(rt);

    ret = eval(ctx, setup_code);
    assert(!MBJS_IsException(ret));
    MBJS_FreeValue(ctx, ret);

    MBJS_ComputeMemoryUsage(rt, &stats);
    MBJS_SetMemoryLimit(rt, (size_t)stats.malloc_size + 128 * 1024);

    ret = eval(ctx, "Object.keys(p)");
    assert(MBJS_IsException(ret));
    MBJS_FreeValue(ctx, ret);
    exception = MBJS_GetException(ctx);
    str = MBJS_ToCString(ctx, exception);
    assert(str);
    /* the trap result has no index 0, so the first key must be rejected */
    assert(!strcmp(str, "TypeError: proxy: properties must be strings or symbols"));
    MBJS_FreeCString(ctx, str);
    MBJS_FreeValue(ctx, exception);
    MBJS_SetMemoryLimit(rt, 0);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static int gop_get_own_property(MBJSContext *ctx, MBJSPropertyDescriptor *desc,
                                MBJSValueConst obj, MBJSAtom prop)
{
    const char *name;
    int found;

    found = 0;
    name = MBJS_AtomToCString(ctx, prop);
    if (!name)
        return -1;
    if (!strcmp(name, "answer")) {
        found = 1;
        *desc = (MBJSPropertyDescriptor){
            .value = MBJS_NewInt32(ctx, 42),
            .flags = MBJS_PROP_C_W_E | MBJS_PROP_HAS_VALUE,
        };
    }
    MBJS_FreeCString(ctx, name);
    return found;
}

static void global_object_prototype(void)
{
    static const char code[] = "answer";
    MBJSValue global_object, proto, ret;
    MBJSClassID class_id;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    int32_t answer;
    int res;

    {
        rt = new_runtime();
        ctx = MBJS_NewContext(rt);
        proto = MBJS_NewObject(ctx);
        assert(MBJS_IsObject(proto));
        MBJSCFunctionListEntry prop = MBJS_PROP_INT32_DEF("answer", 42, MBJS_PROP_C_W_E);
        MBJS_SetPropertyFunctionList(ctx, proto, &prop, 1);
        global_object = MBJS_GetGlobalObject(ctx);
        res = MBJS_SetPrototype(ctx, global_object, proto);
        assert(res == true);
        MBJS_FreeValue(ctx, global_object);
        MBJS_FreeValue(ctx, proto);
        ret = eval(ctx, code);
        assert(!MBJS_IsException(ret));
        assert(MBJS_IsNumber(ret));
        res = MBJS_ToInt32(ctx, &answer, ret);
        assert(res == 0);
        assert(answer == 42);
        MBJS_FreeValue(ctx, ret);
        MBJS_FreeContext(ctx);
        MBJS_FreeRuntime(rt);
    }
    {
        MBJSClassExoticMethods exotic = (MBJSClassExoticMethods){
            .get_own_property = gop_get_own_property,
        };
        MBJSClassDef def = (MBJSClassDef){
            .class_name = "Global Object",
            .exotic = &exotic,
        };
        rt = new_runtime();
        class_id = 0;
        MBJS_NewClassID(rt, &class_id);
        res = MBJS_NewClass(rt, class_id, &def);
        assert(res == 0);
        ctx = MBJS_NewContext(rt);
        proto = MBJS_NewObjectClass(ctx, class_id);
        assert(MBJS_IsObject(proto));
        global_object = MBJS_GetGlobalObject(ctx);
        res = MBJS_SetPrototype(ctx, global_object, proto);
        assert(res == true);
        MBJS_FreeValue(ctx, global_object);
        MBJS_FreeValue(ctx, proto);
        ret = eval(ctx, code);
        assert(!MBJS_IsException(ret));
        assert(MBJS_IsNumber(ret));
        res = MBJS_ToInt32(ctx, &answer, ret);
        assert(res == 0);
        assert(answer == 42);
        MBJS_FreeValue(ctx, ret);
        MBJS_FreeContext(ctx);
        MBJS_FreeRuntime(rt);
    }
}

// https://github.com/mbjs-ng/mbjs/issues/1178
static void slice_string_tocstring(void)
{
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue ret = eval(ctx, "'.'.repeat(16384).slice(1, -1)");
    assert(!MBJS_IsException(ret));
    assert(MBJS_IsString(ret));
    const char *str = MBJS_ToCString(ctx, ret);
    assert(strlen(str) == 16382);
    MBJS_FreeCString(ctx, str);
    MBJS_FreeValue(ctx, ret);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void immutable_array_buffer(void)
{
    MBJSValue obj, ret;
    bool immutable;
    char buf[96];
    int i, v;

    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    for (i = 0; i < 2; i++) {
        obj = MBJS_NewObject(ctx);
        immutable = (i == 0);
        assert(-1 == MBJS_IsImmutableArrayBuffer(MBJS_NULL));
        assert(-1 == MBJS_IsImmutableArrayBuffer(MBJS_UNDEFINED));
        assert(-1 == MBJS_IsImmutableArrayBuffer(obj));
        assert(-1 == MBJS_SetImmutableArrayBuffer(MBJS_NULL, immutable));
        assert(-1 == MBJS_SetImmutableArrayBuffer(MBJS_UNDEFINED, immutable));
        assert(-1 == MBJS_SetImmutableArrayBuffer(obj, immutable));
        MBJS_FreeValue(ctx, obj);
    }
    obj = eval(ctx, "globalThis.ab = new ArrayBuffer(1)");
    assert(!MBJS_IsException(obj));
    assert(MBJS_IsArrayBuffer(obj));
    assert(!MBJS_IsImmutableArrayBuffer(obj));
    for (i = 1; i <= 3; i++) {
        immutable = (i == 2);
        if (i > 1)
            MBJS_SetImmutableArrayBuffer(obj, immutable);
        assert(immutable == MBJS_IsImmutableArrayBuffer(obj));
        snprintf(buf, sizeof(buf),
                 "var ta = new Uint8Array(ab); ta[0] = %d; ta[0]", i);
        ret = eval(ctx, buf);
        assert(!MBJS_IsException(ret));
        assert(MBJS_IsNumber(ret));
        assert(0 == MBJS_ToInt32(ctx, &v, ret));
        MBJS_FreeValue(ctx, ret);
        if (immutable) {
            assert(v != i);
        } else {
            assert(v == i);
        }
    }
    MBJS_FreeValue(ctx, obj);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void *sab_test_alloc(void *opaque, size_t size)
{
    return malloc(size);
}

static void sab_test_free(void *opaque, void *ptr)
{
    free(ptr);
}

static void shared_array_buffer_growth(void)
{
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue ret, exception;

    ret = eval(ctx, "new SharedArrayBuffer(16)");
    assert(!MBJS_IsException(ret));
    MBJS_FreeValue(ctx, ret);

    ret = eval(ctx,
               "const sab = new SharedArrayBuffer(16, { maxByteLength: 16 });"
               "sab.grow(16);"
               "sab.byteLength === 16 && sab.maxByteLength === 16");
    assert(!MBJS_IsException(ret));
    assert(MBJS_IsBool(ret));
    assert(MBJS_VALUE_GET_BOOL(ret));
    MBJS_FreeValue(ctx, ret);

    ret = eval(ctx, "new SharedArrayBuffer(16, { maxByteLength: 16384 })");
    assert(MBJS_IsException(ret));
    assert(MBJS_HasException(ctx));
    exception = MBJS_GetException(ctx);
    assert(MBJS_IsError(exception));
    MBJS_FreeValue(ctx, exception);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);

    MBJSSharedArrayBufferFunctions funcs = {
        .sab_alloc = sab_test_alloc,
        .sab_free = sab_test_free,
        .sab_dup = NULL,
        .sab_opaque = NULL,
    };

    rt = new_runtime();
    MBJS_SetSharedArrayBufferFunctions(rt, &funcs);
    ctx = MBJS_NewContext(rt);
    ret = eval(ctx,
               "const sab = new SharedArrayBuffer(16, { maxByteLength: 16384 });"
               "const u8 = new Uint8Array(sab);"
               "sab.grow(16384);"
               "u8[1024] === 0 && u8.byteLength === 16384");
    assert(!MBJS_IsException(ret));
    assert(MBJS_IsBool(ret));
    assert(MBJS_VALUE_GET_BOOL(ret));
    MBJS_FreeValue(ctx, ret);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void get_uint8array(void)
{
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue val;
    uint8_t *p;
    size_t size;
    uint8_t buf[3] = { 1, 2, 3 };

    val = eval(ctx, "new Uint8Array(0)");
    assert(!MBJS_IsException(val));
    p = MBJS_GetUint8Array(ctx, &size, val);
    assert(p != NULL);
    assert(size == 0);
    MBJS_FreeValue(ctx, val);

    val = MBJS_NewUint8Array(ctx, NULL, 0, NULL, NULL, false);
    assert(!MBJS_IsException(val));
    p = MBJS_GetUint8Array(ctx, &size, val);
    assert(p != NULL);
    assert(size == 0);
    MBJS_FreeValue(ctx, val);

    val = MBJS_NewUint8ArrayCopy(ctx, NULL, 0);
    assert(!MBJS_IsException(val));
    p = MBJS_GetUint8Array(ctx, &size, val);
    assert(p != NULL);
    assert(size == 0);
    MBJS_FreeValue(ctx, val);

    val = MBJS_NewUint8ArrayCopy(ctx, buf, sizeof(buf));
    assert(!MBJS_IsException(val));
    p = MBJS_GetUint8Array(ctx, &size, val);
    assert(p != NULL);
    assert(size == 3);
    assert(p[0] == 1 && p[1] == 2 && p[2] == 3);
    MBJS_FreeValue(ctx, val);

    val = eval(ctx, "new Uint8Array([4, 5, 6])");
    assert(!MBJS_IsException(val));
    p = MBJS_GetUint8Array(ctx, &size, val);
    assert(p != NULL);
    assert(size == 3);
    assert(p[0] == 4 && p[1] == 5 && p[2] == 6);
    MBJS_FreeValue(ctx, val);

    val = eval(ctx, "new Int32Array(4)");
    assert(!MBJS_IsException(val));
    p = MBJS_GetUint8Array(ctx, &size, val);
    assert(p == NULL);
    assert(size == 0);
    MBJS_FreeValue(ctx, val);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void new_symbol(void)
{
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue global = MBJS_GetGlobalObject(ctx);
    MBJSValue sym, ret;

    /* Local symbol with NULL description -> Symbol() */
    sym = MBJS_NewSymbol(ctx, NULL, false);
    assert(!MBJS_IsException(sym));
    assert(MBJS_IsSymbol(sym));
    MBJS_SetPropertyStr(ctx, global, "sym_local_null", sym);

    ret = eval(ctx, "typeof sym_local_null === 'symbol' && sym_local_null.description === undefined && Symbol.keyFor(sym_local_null) === undefined");
    assert(MBJS_IsBool(ret));
    assert(MBJS_VALUE_GET_BOOL(ret));
    MBJS_FreeValue(ctx, ret);

    /* Global symbol with NULL description -> Symbol.for() -> Symbol.for('undefined') */
    sym = MBJS_NewSymbol(ctx, NULL, true);
    assert(!MBJS_IsException(sym));
    assert(MBJS_IsSymbol(sym));
    MBJS_SetPropertyStr(ctx, global, "sym_global_null", sym);

    ret = eval(ctx, "typeof sym_global_null === 'symbol' && sym_global_null.description === 'undefined' && Symbol.keyFor(sym_global_null) === 'undefined'");
    assert(MBJS_IsBool(ret));
    assert(MBJS_VALUE_GET_BOOL(ret));
    MBJS_FreeValue(ctx, ret);

    /* Local symbol with description -> Symbol('test_local') */
    sym = MBJS_NewSymbol(ctx, "test_local", false);
    assert(!MBJS_IsException(sym));
    assert(MBJS_IsSymbol(sym));
    MBJS_SetPropertyStr(ctx, global, "sym_local_str", sym);

    ret = eval(ctx, "sym_local_str.description === 'test_local' && Symbol.keyFor(sym_local_str) === undefined");
    assert(MBJS_IsBool(ret));
    assert(MBJS_VALUE_GET_BOOL(ret));
    MBJS_FreeValue(ctx, ret);

    /* Global symbol with description -> Symbol.for('test_global') */
    sym = MBJS_NewSymbol(ctx, "test_global", true);
    assert(!MBJS_IsException(sym));
    assert(MBJS_IsSymbol(sym));
    MBJS_SetPropertyStr(ctx, global, "sym_global_str", sym);

    ret = eval(ctx, "sym_global_str.description === 'test_global' && Symbol.keyFor(sym_global_str) === 'test_global'");
    assert(MBJS_IsBool(ret));
    assert(MBJS_VALUE_GET_BOOL(ret));
    MBJS_FreeValue(ctx, ret);

    MBJS_FreeValue(ctx, global);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void bulk_free_macros(void) {
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);

    MBJSValue val0 = MBJS_NewObject(ctx);
    MBJSValue val1 = MBJS_NewArray(ctx);
    MBJSValue val2 = MBJS_NewDate(ctx, 0.0);
    
    MBJSMemoryUsage mem_usage;
    MBJS_ComputeMemoryUsage(rt, &mem_usage);
    int obj_count = mem_usage.obj_count;

    MBJS_FreeValues(ctx, val0, val1);
    MBJS_FreeValuesRT(rt, val2);

    // silly atoms to ensure mbjs doesn't find built-ins that match
    MBJSAtom atom0 = MBJS_NewAtom(ctx, "ALL!!");
    MBJSAtom atom1 = MBJS_NewAtom(ctx, "YOUR!!");
    MBJSAtom atom2 = MBJS_NewAtom(ctx, "ATOMS!!");
    MBJSAtom atom3 = MBJS_NewAtom(ctx, "ARE!!");
    MBJSAtom atom4 = MBJS_NewAtom(ctx, "BELONG!!");
    MBJSAtom atom5 = MBJS_NewAtom(ctx, "TO US!!");

    MBJS_ComputeMemoryUsage(rt, &mem_usage);
    assert((obj_count - 3) == mem_usage.obj_count);
    int atom_count = mem_usage.atom_count;

    MBJS_FreeAtoms(ctx, atom1, atom3, atom4);
    MBJS_FreeAtomsRT(rt, atom0, atom2, atom5);

    MBJS_ComputeMemoryUsage(rt, &mem_usage);
    assert((atom_count - 6) == mem_usage.atom_count);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static int detach_free_count;

static void *detach_realloc_func(MBJSRuntime *rt, void *opaque, void *ptr,
                                 size_t size)
{
    if (size == 0) {
        detach_free_count++;
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

static void detach_array_buffer_free_once(void)
{
    MBJSValue obj;
    uint8_t *buf;

    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);

    detach_free_count = 0;
    buf = malloc(8);
    obj = MBJS_NewArrayBuffer(ctx, buf, 8, /*max_len*/0, detach_realloc_func,
                            NULL, false);
    assert(MBJS_IsArrayBuffer(obj));

    /* detaching releases the backing store exactly once */
    MBJS_DetachArrayBuffer(ctx, obj);
    assert(detach_free_count == 1);

    /* finalizing the detached buffer must not release it a second time */
    MBJS_FreeValue(ctx, obj);
    assert(detach_free_count == 1);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void *realloc_array_buffer_data(MBJSRuntime *rt, void *opaque, void *ptr,
                                       size_t size)
{
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

static void typed_array_sort_index_overflow(void)
{
    const size_t len = 0x40000010; /* 2**30 + 16 */
    MBJSMemoryUsage stats;
    MBJSValue global, buffer, ret;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    uint8_t *buf;

    /* the data is never read, but keep it real memory so a regression
       corrupts the heap in a way ASan can see rather than segfaulting */
    buf = calloc(1, len);
    rt = new_runtime();
    ctx = MBJS_NewContext(rt);

    buffer = MBJS_NewArrayBuffer(ctx, buf, len, /*max_len*/0,
                               realloc_array_buffer_data, NULL, false);
    assert(MBJS_IsArrayBuffer(buffer));
    global = MBJS_GetGlobalObject(ctx);
    MBJS_SetPropertyStr(ctx, global, "buf", buffer);
    MBJS_FreeValue(ctx, global);

    /* Cap memory so that the correctly computed 4 GB index array also
       fails to allocate on 64 bit platforms; the undersized allocation a
       32 bit platform computes fits well within the cap. */
    MBJS_ComputeMemoryUsage(rt, &stats);
    MBJS_SetMemoryLimit(rt, (size_t)stats.malloc_size + 1024 * 1024);

    ret = eval(ctx, "new Uint8Array(buf).sort((a, b) => a - b)");
    assert(MBJS_IsException(ret));
    assert(MBJS_HasException(ctx));
    MBJS_FreeValue(ctx, ret);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static int transfer_free_count;
static int transfer_realloc_count;

static void *transfer_realloc_func(MBJSRuntime *rt, void *opaque, void *ptr,
                                   size_t size)
{
    if (size == 0) {
        transfer_free_count++;
        free(ptr);
        return NULL;
    }
    transfer_realloc_count++;
    return realloc(ptr, size);
}

/* a realloc function that can only free, never resize */
static void *failing_realloc_func(MBJSRuntime *rt, void *opaque, void *ptr,
                                  size_t size)
{
    if (size == 0) {
        transfer_free_count++;
        free(ptr);
    }
    return NULL;
}

static MBJSValue make_external_ab(MBJSContext *ctx, uint8_t **pbuf, size_t len,
                                size_t max_len,
                                MBJSReallocArrayBufferDataFunc *realloc_func)
{
    uint8_t *buf = malloc(max_len > len ? max_len : len);
    memset(buf, 0xAB, len);
    MBJSValue obj = MBJS_NewArrayBuffer(ctx, buf, len, max_len, realloc_func, NULL,
                                    /*is_shared*/false);
    assert(MBJS_IsArrayBuffer(obj));
    MBJSValue global = MBJS_GetGlobalObject(ctx);
    MBJS_SetPropertyStr(ctx, global, "ab", MBJS_DupValue(ctx, obj));
    MBJS_FreeValue(ctx, global);
    *pbuf = buf;
    return obj;
}

static void transfer_external_array_buffer(void)
{
    MBJSValue obj, ret, exc, det;
    const char *s;
    uint8_t *buf, *p;
    size_t i, size;

    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);

    // same-length transfer succeeds and reuses the backing store; each variant
    // detaches the source, so use a fresh buffer per iteration
    {
        static const char *const exprs[] = {
            "ab.transfer()", "ab.transfer(8)", "ab.transferToFixedLength()",
            "ab.transferToFixedLength(8)", "ab.transferToImmutable()",
        };
        for (i = 0; i < countof(exprs); i++) {
            transfer_free_count = 0;
            transfer_realloc_count = 0;
            obj = make_external_ab(ctx, &buf, 8, /*max_len*/0,
                                   transfer_realloc_func);

            ret = eval(ctx, exprs[i]);
            assert(!MBJS_IsException(ret));
            assert(MBJS_IsArrayBuffer(ret));
            assert(transfer_realloc_count == 0);
            p = MBJS_GetArrayBuffer(ctx, &size, ret);
            assert(p == buf);
            assert(size == 8);
            assert(p[0] == 0xAB && p[7] == 0xAB);
            det = eval(ctx, "ab.detached");
            assert(MBJS_IsBool(det));
            assert(MBJS_VALUE_GET_BOOL(det));
            MBJS_FreeValue(ctx, det);
            assert(transfer_free_count == 0);
            MBJS_FreeValue(ctx, ret);
            assert(transfer_free_count == 1);

            ret = eval(ctx, "globalThis.ab = undefined");
            MBJS_FreeValue(ctx, ret);
            MBJS_FreeValue(ctx, obj);
            assert(transfer_free_count == 1);
        }
    }

    // a length-changing transfer resizes the external store through the
    // realloc function and hands it over to the new ArrayBuffer
    {
        static const struct {
            const char *code;
            size_t len;
        } cases[] = {
            { "ab.transfer(4)", 4 },
            { "ab.transfer(16)", 16 },
            { "ab.transferToFixedLength(4)", 4 },
            { "ab.transferToImmutable(16)", 16 },
        };
        for (i = 0; i < countof(cases); i++) {
            transfer_free_count = 0;
            transfer_realloc_count = 0;
            obj = make_external_ab(ctx, &buf, 8, /*max_len*/0,
                                   transfer_realloc_func);

            ret = eval(ctx, cases[i].code);
            assert(!MBJS_IsException(ret));
            assert(MBJS_IsArrayBuffer(ret));
            assert(transfer_realloc_count == 1);
            assert(transfer_free_count == 0);
            p = MBJS_GetArrayBuffer(ctx, &size, ret);
            assert(size == cases[i].len);
            assert(p[0] == 0xAB && p[3] == 0xAB);
            if (cases[i].len > 8)
                assert(p[8] == 0 && p[15] == 0); // the grown tail is zeroed
            det = eval(ctx, "ab.detached");
            assert(MBJS_IsBool(det));
            assert(MBJS_VALUE_GET_BOOL(det));
            MBJS_FreeValue(ctx, det);

            // the source handed the store over; releasing the new AB frees it
            MBJS_FreeValue(ctx, ret);
            assert(transfer_free_count == 1);

            ret = eval(ctx, "globalThis.ab = undefined");
            MBJS_FreeValue(ctx, ret);
            MBJS_FreeValue(ctx, obj);
            assert(transfer_free_count == 1);
        }
    }

    // a transfer that fails to realloc throws and leaves the source alone
    {
        transfer_free_count = 0;
        obj = make_external_ab(ctx, &buf, 8, /*max_len*/0,
                               failing_realloc_func);

        ret = eval(ctx, "ab.transfer(16)");
        assert(MBJS_IsException(ret));
        MBJS_FreeValue(ctx, ret);
        exc = MBJS_GetException(ctx);
        assert(MBJS_IsError(exc));
        s = MBJS_ToCString(ctx, exc);
        assert(s);
        assert(strstr(s, "out of memory"));
        MBJS_FreeCString(ctx, s);
        MBJS_FreeValue(ctx, exc);

        det = eval(ctx, "ab.detached");
        assert(MBJS_IsBool(det));
        assert(!MBJS_VALUE_GET_BOOL(det));
        MBJS_FreeValue(ctx, det);
        p = MBJS_GetArrayBuffer(ctx, &size, obj);
        assert(p == buf && size == 8 && p[0] == 0xAB);
        assert(transfer_free_count == 0);

        ret = eval(ctx, "globalThis.ab = undefined");
        MBJS_FreeValue(ctx, ret);
        MBJS_FreeValue(ctx, obj);
        assert(transfer_free_count == 1);
    }

    // transfer(0) changes the length but doesn't realloc: it detaches the
    // source (freeing the external store) and yields a fresh empty AB
    {
        transfer_free_count = 0;
        transfer_realloc_count = 0;
        obj = make_external_ab(ctx, &buf, 8, /*max_len*/0,
                               transfer_realloc_func);

        ret = eval(ctx, "ab.transfer(0)");
        assert(!MBJS_IsException(ret));
        assert(MBJS_IsArrayBuffer(ret));
        p = MBJS_GetArrayBuffer(ctx, &size, ret);
        assert(size == 0);
        assert(transfer_free_count == 1);
        MBJS_FreeValue(ctx, ret);

        det = eval(ctx, "ab.detached");
        assert(MBJS_IsBool(det));
        assert(MBJS_VALUE_GET_BOOL(det));
        MBJS_FreeValue(ctx, det);

        ret = eval(ctx, "globalThis.ab = undefined");
        MBJS_FreeValue(ctx, ret);
        MBJS_FreeValue(ctx, obj);
        assert(transfer_free_count == 1);
    }

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void resize_external_array_buffer(void)
{
    MBJSValue obj, global, ret, exc, det;
    const char *s;
    uint8_t *buf, *p;
    size_t size;

    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);

    transfer_free_count = 0;
    transfer_realloc_count = 0;
    obj = make_external_ab(ctx, &buf, 8, /*max_len*/16, transfer_realloc_func);

    ret = eval(ctx, "ab.resizable && ab.maxByteLength === 16");
    assert(MBJS_IsBool(ret));
    assert(MBJS_VALUE_GET_BOOL(ret));
    MBJS_FreeValue(ctx, ret);

    // growing reallocs through the callback and zeroes the new tail
    ret = eval(ctx, "ab.resize(16), ab.byteLength");
    assert(!MBJS_IsException(ret));
    assert(MBJS_VALUE_GET_INT(ret) == 16);
    MBJS_FreeValue(ctx, ret);
    assert(transfer_realloc_count == 1);
    p = MBJS_GetArrayBuffer(ctx, &size, obj);
    assert(size == 16);
    assert(p[0] == 0xAB && p[7] == 0xAB);
    assert(p[8] == 0 && p[15] == 0);

    // and so does shrinking
    ret = eval(ctx, "ab.resize(4), ab.byteLength");
    assert(!MBJS_IsException(ret));
    assert(MBJS_VALUE_GET_INT(ret) == 4);
    MBJS_FreeValue(ctx, ret);
    assert(transfer_realloc_count == 2);
    p = MBJS_GetArrayBuffer(ctx, &size, obj);
    assert(size == 4);
    assert(p[0] == 0xAB && p[3] == 0xAB);

    // resizing past maxByteLength still throws, without calling the callback
    ret = eval(ctx, "ab.resize(17)");
    assert(MBJS_IsException(ret));
    MBJS_FreeValue(ctx, ret);
    exc = MBJS_GetException(ctx);
    s = MBJS_ToCString(ctx, exc);
    assert(s);
    assert(strstr(s, "invalid array buffer length"));
    MBJS_FreeCString(ctx, s);
    MBJS_FreeValue(ctx, exc);
    assert(transfer_realloc_count == 2);

    // a resizable external AB carries its maxByteLength across a transfer
    ret = eval(ctx, "ab.transfer(8)");
    assert(!MBJS_IsException(ret));
    assert(transfer_realloc_count == 3);
    det = eval(ctx, "ab.detached");
    assert(MBJS_IsBool(det));
    assert(MBJS_VALUE_GET_BOOL(det));
    MBJS_FreeValue(ctx, det);
    global = MBJS_GetGlobalObject(ctx);
    MBJS_SetPropertyStr(ctx, global, "ab2", ret);
    MBJS_FreeValue(ctx, global);
    ret = eval(ctx, "ab2.resizable && ab2.maxByteLength === 16");
    assert(MBJS_IsBool(ret));
    assert(MBJS_VALUE_GET_BOOL(ret));
    MBJS_FreeValue(ctx, ret);
    ret = eval(ctx, "ab2.resize(16), ab2.byteLength");
    assert(!MBJS_IsException(ret));
    assert(MBJS_VALUE_GET_INT(ret) == 16);
    MBJS_FreeValue(ctx, ret);
    assert(transfer_realloc_count == 4);

    assert(transfer_free_count == 0);
    ret = eval(ctx, "globalThis.ab = globalThis.ab2 = undefined");
    MBJS_FreeValue(ctx, ret);
    MBJS_FreeValue(ctx, obj);
    assert(transfer_free_count == 1);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

/* hands the buffer over to the runtime allocator, the way mbjs manages the
   memory of ArrayBuffers it allocates itself */
static void *runtime_realloc_func(MBJSRuntime *rt, void *opaque, void *ptr,
                                  size_t size)
{
    return mbjs_realloc_rt(rt, ptr, size);
}

static void transfer_default_managed_array_buffer(void)
{
    MBJSValue obj, global, ret, det;
    uint8_t *buf, *p;
    size_t size;

    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);

    buf = mbjs_malloc(ctx, 8);
    memset(buf, 0xAB, 8);
    obj = MBJS_NewArrayBuffer(ctx, buf, 8, /*max_len*/0,
                            runtime_realloc_func, NULL, false);
    assert(MBJS_IsArrayBuffer(obj));
    global = MBJS_GetGlobalObject(ctx);
    MBJS_SetPropertyStr(ctx, global, "ab", MBJS_DupValue(ctx, obj));
    MBJS_FreeValue(ctx, global);

    ret = eval(ctx, "ab.transfer(16)");
    assert(!MBJS_IsException(ret));
    assert(MBJS_IsArrayBuffer(ret));
    p = MBJS_GetArrayBuffer(ctx, &size, ret);
    assert(size == 16);
    assert(p[0] == 0xAB && p[7] == 0xAB);
    assert(p[8] == 0 && p[15] == 0);
    det = eval(ctx, "ab.detached");
    assert(MBJS_IsBool(det));
    assert(MBJS_VALUE_GET_BOOL(det));
    MBJS_FreeValue(ctx, det);
    MBJS_FreeValue(ctx, ret);
    ret = eval(ctx, "globalThis.ab = undefined");
    MBJS_FreeValue(ctx, ret);
    MBJS_FreeValue(ctx, obj);

    buf = mbjs_malloc(ctx, 8);
    memset(buf, 0xCD, 8);
    obj = MBJS_NewArrayBuffer(ctx, buf, 8, /*max_len*/0,
                            runtime_realloc_func, NULL, false);
    global = MBJS_GetGlobalObject(ctx);
    MBJS_SetPropertyStr(ctx, global, "ab", MBJS_DupValue(ctx, obj));
    MBJS_FreeValue(ctx, global);

    ret = eval(ctx, "ab.transferToFixedLength(4)");
    assert(!MBJS_IsException(ret));
    p = MBJS_GetArrayBuffer(ctx, &size, ret);
    assert(size == 4);
    assert(p[0] == 0xCD && p[3] == 0xCD);
    MBJS_FreeValue(ctx, ret);
    ret = eval(ctx, "globalThis.ab = undefined");
    MBJS_FreeValue(ctx, ret);
    MBJS_FreeValue(ctx, obj);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void get_class_name(void)
{
    static const struct {
        const char *code;
        const char *name;
    } builtins[] = {
        { "({})",               "Object" },
        { "[]",                 "Array" },
        { "new Error()",        "Error" },
        { "new Date()",         "Date" },
        { "/re/",               "RegExp" },
        { "new Map()",          "Map" },
        { "new ArrayBuffer(0)", "ArrayBuffer" },
        { "new Uint8Array(0)",  "Uint8Array" },
        { "(function(){})",     "Function" },
    };
    MBJSClassDef def = (MBJSClassDef){ .class_name = "MyClass" };
    MBJSClassID class_id, unregistered_class_id;
    MBJSAtom atom, expected;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    const char *s;
    MBJSValue obj;
    size_t i;

    rt = new_runtime();
    class_id = 0;
    MBJS_NewClassID(rt, &class_id);
    assert(0 == MBJS_NewClass(rt, class_id, &def));

    /* handed out by MBJS_NewClassID() but never passed to MBJS_NewClass() */
    unregistered_class_id = 0;
    MBJS_NewClassID(rt, &unregistered_class_id);

    ctx = MBJS_NewContext(rt);

    /* the name of a class registered from C, not its class id */
    expected = MBJS_NewAtom(ctx, "MyClass");
    atom = MBJS_GetClassName(rt, class_id);
    assert(atom == expected);
    s = MBJS_AtomToCString(ctx, atom);
    assert(s);
    assert(!strcmp(s, "MyClass"));
    MBJS_FreeCString(ctx, s);
    MBJS_FreeAtom(ctx, atom);
    MBJS_FreeAtom(ctx, expected);

    /* the names of the built-in classes */
    for (i = 0; i < countof(builtins); i++) {
        obj = eval(ctx, builtins[i].code);
        assert(!MBJS_IsException(obj));
        atom = MBJS_GetClassName(rt, MBJS_GetClassID(obj));
        assert(atom != MBJS_ATOM_NULL);
        s = MBJS_AtomToCString(ctx, atom);
        assert(s);
        assert(!strcmp(s, builtins[i].name));
        MBJS_FreeCString(ctx, s);
        MBJS_FreeAtom(ctx, atom);
        MBJS_FreeValue(ctx, obj);
    }

    /* class ids without a registered class have no name */
    assert(MBJS_ATOM_NULL == MBJS_GetClassName(rt, MBJS_INVALID_CLASS_ID));
    assert(MBJS_ATOM_NULL == MBJS_GetClassName(rt, unregistered_class_id));
    assert(MBJS_ATOM_NULL == MBJS_GetClassName(rt, class_id + 4096));

    /* the caller owns the returned atom; the class keeps its own reference,
       so releasing it repeatedly doesn't free the name out from under it */
    for (i = 0; i < 64; i++)
        MBJS_FreeAtom(ctx, MBJS_GetClassName(rt, class_id));
    atom = MBJS_GetClassName(rt, class_id);
    s = MBJS_AtomToCString(ctx, atom);
    assert(s);
    assert(!strcmp(s, "MyClass"));
    MBJS_FreeCString(ctx, s);
    MBJS_FreeAtom(ctx, atom);

    /* every registered class has a name, and it is a name rather than a
       number: returning the class id instead produced a valid-looking atom,
       either an unrelated predefined one for a small id or the id spelled out
       in decimal for a large one, so check the whole table rather than the
       handful of classes spelled out above */
    {
        MBJSClassID id;
        int registered = 0;

        for (id = 1; id < class_id + 16; id++) {
            char decimal[32];
            if (!MBJS_IsRegisteredClass(rt, id))
                continue;
            registered++;
            atom = MBJS_GetClassName(rt, id);
            assert(atom != MBJS_ATOM_NULL);
            s = MBJS_AtomToCString(ctx, atom);
            assert(s);
            // a few internal classes are deliberately unnamed, but none is
            // named after a number
            snprintf(decimal, sizeof(decimal), "%u", (unsigned)id);
            assert(strcmp(s, decimal));
            assert(s[0] < '0' || s[0] > '9');
            MBJS_FreeCString(ctx, s);
            MBJS_FreeAtom(ctx, atom);
        }
        /* the built-ins alone are far more than this */
        assert(registered > 20);
    }

    /* two classes sharing a name share the interned atom, and the name does
       not depend on which context asks */
    {
        MBJSClassDef def2 = (MBJSClassDef){ .class_name = "MyClass" };
        MBJSClassID class_id2 = 0;
        MBJSContext *ctx2;
        MBJSAtom a1, a2;

        MBJS_NewClassID(rt, &class_id2);
        assert(0 == MBJS_NewClass(rt, class_id2, &def2));
        assert(class_id2 != class_id);

        a1 = MBJS_GetClassName(rt, class_id);
        a2 = MBJS_GetClassName(rt, class_id2);
        assert(a1 == a2);
        MBJS_FreeAtom(ctx, a1);
        MBJS_FreeAtom(ctx, a2);

        ctx2 = MBJS_NewContext(rt);
        a1 = MBJS_GetClassName(rt, class_id);
        a2 = MBJS_GetClassName(rt, class_id);
        assert(a1 == a2);
        MBJS_FreeAtom(ctx2, a1);
        MBJS_FreeAtom(ctx2, a2);
        MBJS_FreeContext(ctx2);
    }

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

void object_from(void)
{
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue t0 = MBJS_NewObject(ctx);
    assert(!MBJS_IsException(t0));
    assert(MBJS_IsObject(t0));
    MBJSAtom prop = MBJS_NewAtomLen(ctx, "prop", 4);
    assert(prop != MBJS_ATOM_NULL);
    MBJSValue val = MBJS_NULL;
    MBJSValue t1 = MBJS_NewObjectFrom(ctx, 1, &prop, &val);
    assert(!MBJS_IsException(t1));
    assert(MBJS_IsObject(t1));
    uint32_t old_shape_hash_count = mbjs_std_cmd(/*GetShapeHashCount*/4, rt);
    MBJSValue t2 = MBJS_NewObjectFrom(ctx, 1, &prop, &val);
    assert(!MBJS_IsException(t2));
    assert(MBJS_IsObject(t2));
    uint32_t new_shape_hash_count = mbjs_std_cmd(/*GetShapeHashCount*/4, rt);
    assert(old_shape_hash_count == new_shape_hash_count);
    MBJS_FreeAtom(ctx, prop);
    MBJS_FreeValue(ctx, t2);
    MBJS_FreeValue(ctx, t1);
    MBJS_FreeValue(ctx, t0);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

void add_intrinsic_bigint(void)
{
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContextRaw(rt);
    MBJS_AddIntrinsicBaseObjects(ctx);
    MBJS_AddIntrinsicBigInt(ctx);
    assert(!MBJS_HasException(ctx));
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

void new_typed_array(void)
{
    MBJSValueConst argv[4];
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    uint8_t *buf = mbjs_malloc(ctx, 8);
    MBJSValue ab = MBJS_NewArrayBuffer(ctx, buf, 8, /*max_len*/0, NULL, NULL, false);
    assert(MBJS_IsArrayBuffer(ab));
    for (int argc = 0; argc < (int)countof(argv); argc++) {
        MBJSValue ret = MBJS_NewTypedArray(ctx, argc, argv, MBJS_TYPED_ARRAY_UINT8);
        assert(!MBJS_IsException(ret));
        assert(MBJS_IsObject(ret));
        MBJS_FreeValue(ctx, ret);
        argv[argc] = MBJS_UNDEFINED;
        argv[0] = ab;
    }
    MBJS_FreeValue(ctx, ab);
    mbjs_free(ctx, buf);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

void private_symbols(void)
{
    MBJSRuntime *rt = new_runtime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue pub = MBJS_NewSymbol(ctx, "public", /*is_global*/false);
    MBJSValue priv = MBJS_NewPrivateSymbol(ctx, "private");
    assert(MBJS_IsSymbol(pub));
    assert(MBJS_IsSymbol(priv));
    MBJSValue obj = MBJS_NewObject(ctx);
    assert(MBJS_IsObject(obj));
    assert(true == MBJS_SetPropertyValue(ctx, obj, pub, MBJS_TRUE, MBJS_PROP_C_W_E));
    assert(true == MBJS_SetPropertyValue(ctx, obj, priv, MBJS_FALSE, MBJS_PROP_C_W_E));
    MBJSValue global_object = MBJS_GetGlobalObject(ctx);
    assert(true == MBJS_SetPropertyStr(ctx, global_object, "o", MBJS_DupValue(ctx, obj)));
    MBJS_FreeValue(ctx, global_object);
    MBJSValue result = eval(ctx, "Object.getOwnPropertySymbols(o)");
    assert(MBJS_IsArray(result));
    int64_t length = -1;
    assert(0 == MBJS_GetLength(ctx, result, &length));
    assert(length == 1);
    MBJSValue item = MBJS_GetPropertyUint32(ctx, result, 0);
    assert(MBJS_IsSymbol(item));
    assert(MBJS_IsSameValue(ctx, item, pub));
    MBJS_FreeValue(ctx, item);
    MBJS_FreeValue(ctx, result);
    result = MBJS_GetPropertyValue(ctx, obj, MBJS_DupValue(ctx, pub));
    assert(MBJS_IsBool(result));
    assert(MBJS_IsSameValue(ctx, result, MBJS_TRUE));
    result = MBJS_GetPropertyValue(ctx, obj, MBJS_DupValue(ctx, priv));
    assert(MBJS_IsBool(result));
    assert(MBJS_IsSameValue(ctx, result, MBJS_FALSE));
    MBJS_FreeValue(ctx, obj);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

// https://github.com/mbjs-ng/mbjs/issues/1181
// MBJS_NewContext() must fail cleanly, not crash or leak, whichever of its
// allocations runs out of memory.
static void new_context_low_memory(void)
{
    MBJSMemoryUsage stats;
    size_t base, headroom;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    int all, ok;

    all = ok = 0;
    rt = new_runtime();
    MBJS_ComputeMemoryUsage(rt, &stats);
    base = (size_t)stats.malloc_size;
    // Small steps first, so that each of the first allocations gets its turn
    // to fail, then bigger ones until a few contexts fit.
    for (headroom = 0; ok < 4 && headroom < 4 * 1024 * 1024;
         headroom += headroom < 8192 ? 8 : 1024) {
        MBJS_SetMemoryLimit(rt, base + headroom);
        ctx = MBJS_NewContext(rt); // expected to fail, not to crash
        if (ctx) {
            MBJS_FreeContext(ctx);
            ok++;
        }
        all++;
    }
    MBJS_SetMemoryLimit(rt, 0);
    MBJS_FreeRuntime(rt);
    assert(ok > 0);     // expect some successes...
    assert(ok < all);   // ...but not all
}

// running out of memory in JSON.stringify must not leak the array element
// that was being converted
static void json_stringify_oom(void)
{
    static const char setup_code[] =
        "globalThis.f = () => JSON.stringify([{}]);\n"
        "f();\n"; // JSON.stringify is set up on first access, do it now
    MBJSValue global_object, func, ret;
    MBJSMemoryUsage stats;
    uint32_t headroom;
    MBJSRuntime *rt;
    MBJSContext *ctx;

    rt = new_runtime();
    ctx = MBJS_NewContext(rt);
    global_object = MBJS_GetGlobalObject(ctx);

    ret = eval(ctx, setup_code);
    assert(!MBJS_IsException(ret));
    MBJS_FreeValue(ctx, ret);

    func = MBJS_GetPropertyStr(ctx, global_object, "f");
    assert(MBJS_IsFunction(ctx, func));

    for (headroom = 0; headroom < 2048; headroom++) {
        MBJS_ComputeMemoryUsage(rt, &stats);
        MBJS_SetMemoryLimit(rt, (size_t)stats.malloc_size + headroom);
        ret = MBJS_Call(ctx, func, MBJS_UNDEFINED, 0, NULL);
        MBJS_SetMemoryLimit(rt, 0);
        MBJS_FreeValue(ctx, ret);
        MBJS_FreeValue(ctx, MBJS_GetException(ctx));
    }

    MBJS_FreeValue(ctx, func);
    MBJS_FreeValue(ctx, global_object);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

struct dynamic_import_state {
    int import_calls;
    int attribute_calls;
    bool defer;
    char *basename;
    char *filename;
    MBJSValue token;
    MBJSValue attributes;
};

static MBJSModuleDef *dynamic_import_loader(MBJSContext *ctx, const char *name,
                                          void *opaque, MBJSValueConst attributes)
{
    struct dynamic_import_state *s = opaque;
    if (!strcmp(name, "missing")) {
        MBJS_ThrowReferenceError(ctx, "no such module");
        return NULL;
    }
    if (MBJS_IsObject(attributes)) {
        MBJSValue type = MBJS_GetPropertyStr(ctx, attributes, "type");
        const char *str = MBJS_ToCString(ctx, type);
        assert(str);
        assert(!strcmp(str, "x"));
        MBJS_FreeCString(ctx, str);
        MBJS_FreeValue(ctx, type);
        s->attribute_calls++;
    }
    static const char code[] = "export const v = 42";
    MBJSValue ret = MBJS_Eval(ctx, code, strlen(code), name,
                          MBJS_EVAL_TYPE_MODULE|MBJS_EVAL_FLAG_COMPILE_ONLY);
    assert(!MBJS_IsException(ret));
    MBJSModuleDef *m = MBJS_VALUE_GET_PTR(ret);
    MBJS_FreeValue(ctx, ret);
    return m;
}

static void dynamic_import_func(MBJSContext *ctx, const char *basename,
                                const char *filename, MBJSValue token,
                                MBJSValueConst attributes, void *opaque)
{
    struct dynamic_import_state *s = opaque;
    s->import_calls++;
    if (s->defer) {
        s->basename = strdup(basename);
        s->filename = strdup(filename);
        assert(s->basename);
        assert(s->filename);
        s->token = token;
        s->attributes = MBJS_DupValue(ctx, attributes);
        return;
    }
    MBJSValue exception = MBJS_UNDEFINED;
    MBJSValue promise = MBJS_LoadModule(ctx, basename, filename, attributes);
    if (MBJS_IsException(promise))
        exception = MBJS_GetException(ctx);
    assert(0 == MBJS_FinishDynamicImport(ctx, token, promise, exception));
}

static MBJSRuntime *dynamic_import_runtime(struct dynamic_import_state *s)
{
    MBJSRuntime *rt = new_runtime();
    MBJS_SetModuleLoaderFunc2(rt, NULL, dynamic_import_loader, NULL, s);
    MBJS_SetDynamicImportFunc(rt, dynamic_import_func, s);
    return rt;
}

static void dynamic_import_drain_jobs(MBJSRuntime *rt)
{
    MBJSContext *ctx;
    while (MBJS_ExecutePendingJob(rt, &ctx) > 0)
        ;
}

static void dynamic_import_assert_namespace(MBJSContext *ctx, MBJSValueConst promise)
{
    assert(MBJS_PROMISE_FULFILLED == MBJS_PromiseState(ctx, promise));
    MBJSValue ns = MBJS_PromiseResult(ctx, promise);
    MBJSValue v = MBJS_GetPropertyStr(ctx, ns, "v");
    assert(MBJS_IsNumber(v));
    assert(42 == MBJS_VALUE_GET_INT(v));
    MBJS_FreeValue(ctx, v);
    MBJS_FreeValue(ctx, ns);
}

static void dynamic_import_sync_ok(void)
{
    struct dynamic_import_state s = {0};
    MBJSRuntime *rt = dynamic_import_runtime(&s);
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue promise = eval(ctx, "import('dep', {with: {type: 'x'}})");
    assert(MBJS_PROMISE_PENDING == MBJS_PromiseState(ctx, promise));
    dynamic_import_drain_jobs(rt);
    assert(1 == s.import_calls);
    assert(1 == s.attribute_calls);
    dynamic_import_assert_namespace(ctx, promise);
    MBJS_FreeValue(ctx, promise);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void dynamic_import_sync_missing(void)
{
    struct dynamic_import_state s = {0};
    MBJSRuntime *rt = dynamic_import_runtime(&s);
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue promise = eval(ctx, "import('missing')");
    dynamic_import_drain_jobs(rt);
    assert(1 == s.import_calls);
    assert(MBJS_PROMISE_REJECTED == MBJS_PromiseState(ctx, promise));
    MBJSValue reason = MBJS_PromiseResult(ctx, promise);
    assert(MBJS_IsError(reason));
    MBJS_FreeValue(ctx, reason);
    MBJS_FreeValue(ctx, promise);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void dynamic_import_deferred(void)
{
    struct dynamic_import_state s = {.defer = true};
    MBJSRuntime *rt = dynamic_import_runtime(&s);
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue promise = eval(ctx, "import('dep', {with: {type: 'x'}})");
    dynamic_import_drain_jobs(rt);
    assert(1 == s.import_calls);
    assert(0 == s.attribute_calls);
    assert(MBJS_PROMISE_PENDING == MBJS_PromiseState(ctx, promise));
    // token and attributes must survive a collection while the import is parked
    MBJS_RunGC(rt);
    MBJSValue module_promise = MBJS_LoadModule(ctx, s.basename, s.filename,
                                           s.attributes);
    assert(!MBJS_IsException(module_promise));
    assert(0 == MBJS_FinishDynamicImport(ctx, s.token, module_promise,
                                       MBJS_UNDEFINED));
    MBJS_FreeValue(ctx, s.attributes);
    free(s.basename);
    free(s.filename);
    dynamic_import_drain_jobs(rt);
    assert(1 == s.attribute_calls);
    dynamic_import_assert_namespace(ctx, promise);
    MBJS_FreeValue(ctx, promise);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static void dynamic_import_deferred_reject(void)
{
    struct dynamic_import_state s = {.defer = true};
    MBJSRuntime *rt = dynamic_import_runtime(&s);
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJSValue promise = eval(ctx, "import('dep')");
    dynamic_import_drain_jobs(rt);
    assert(MBJS_PROMISE_PENDING == MBJS_PromiseState(ctx, promise));
    MBJSValue error = MBJS_NewError(ctx);
    assert(1 == MBJS_SetPropertyStr(ctx, error, "tag", MBJS_NewInt32(ctx, 7)));
    assert(0 == MBJS_FinishDynamicImport(ctx, s.token, MBJS_EXCEPTION, error));
    MBJS_FreeValue(ctx, s.attributes);
    free(s.basename);
    free(s.filename);
    dynamic_import_drain_jobs(rt);
    assert(MBJS_PROMISE_REJECTED == MBJS_PromiseState(ctx, promise));
    MBJSValue reason = MBJS_PromiseResult(ctx, promise);
    MBJSValue tag = MBJS_GetPropertyStr(ctx, reason, "tag");
    assert(7 == MBJS_VALUE_GET_INT(tag));
    MBJS_FreeValue(ctx, tag);
    MBJS_FreeValue(ctx, reason);
    MBJS_FreeValue(ctx, promise);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

int main(void)
{
    cfunctions();
    sync_call();
    async_call();
    async_call_stack_overflow();
    raw_context_global_var();
    is_array();
    module_serde();
    module_unhandled_rejection();
    dynamic_import_sync_ok();
    dynamic_import_sync_missing();
    dynamic_import_deferred();
    dynamic_import_deferred_reject();
    promise_mark_as_handled();
    promise_then();
    runtime_cstring_free();
    utf16_string();
    weak_map_gc_check();
    promise_hook();
    dump_memory_usage();
    new_errors();
    dom_exception_added_twice();
    large_allocation_accounting();
    backtrace_oom_current_exception();
    backtrace_oom_callsite_array();
    proxy_own_keys_huge_length();
    global_object_prototype();
    slice_string_tocstring();
    immutable_array_buffer();
    shared_array_buffer_growth();
    get_uint8array();
    new_symbol();
    bulk_free_macros();
    detach_array_buffer_free_once();
    typed_array_sort_index_overflow();
    transfer_external_array_buffer();
    resize_external_array_buffer();
    transfer_default_managed_array_buffer();
    get_class_name();
    object_from();
    add_intrinsic_bigint();
    new_typed_array();
    std_eval_interrupt_handler();
    private_symbols();
    new_context_low_memory();
    json_stringify_oom();
    return 0;
}
