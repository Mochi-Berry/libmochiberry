// note: file is not actually compiled, only checked for C syntax errors
#include "mbjs.h"

int main(void)
{
    MBJSRuntime *rt = MBJS_NewRuntime();
    MBJSContext *ctx = MBJS_NewContext(rt);
    MBJS_FreeValue(ctx, MBJS_NAN);
    MBJS_FreeValue(ctx, MBJS_UNDEFINED);
    MBJS_FreeValue(ctx, MBJS_NewFloat64(ctx, 42));
    // not a legal way of using MBJS_MKPTR but this is here
    // to have the compiler syntax-check its definition
    MBJS_FreeValue(ctx, MBJS_MKPTR(MBJS_TAG_UNINITIALIZED, 0));
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
    return 0;
}
