/*
 * mbjs: Example of C module with a class
 *
 * Copyright (c) 2019 Fabrice Bellard
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

#include <math.h>

#include <mbjs.h>

#define countof(x) (sizeof(x) / sizeof((x)[0]))

/* Point Class */

typedef struct {
    int x;
    int y;
} MBJSPointData;

static MBJSClassID mbjs_point_class_id;

static void mbjs_point_finalizer(MBJSRuntime *rt, MBJSValue val)
{
    MBJSPointData *s = MBJS_GetOpaque(val, mbjs_point_class_id);
    /* Note: 's' can be NULL in case MBJS_SetOpaque() was not called */
    mbjs_free_rt(rt, s);
}

static MBJSValue mbjs_point_ctor(MBJSContext *ctx,
                             MBJSValue new_target,
                             int argc, MBJSValue *argv)
{
    MBJSPointData *s;
    MBJSValue obj = MBJS_UNDEFINED;
    MBJSValue proto;

    s = mbjs_mallocz(ctx, sizeof(*s));
    if (!s)
        return MBJS_EXCEPTION;
    if (MBJS_ToInt32(ctx, &s->x, argv[0]))
        goto fail;
    if (MBJS_ToInt32(ctx, &s->y, argv[1]))
        goto fail;
    /* using new_target to get the prototype is necessary when the
       class is extended. */
    proto = MBJS_GetPropertyStr(ctx, new_target, "prototype");
    if (MBJS_IsException(proto))
        goto fail;
    obj = MBJS_NewObjectProtoClass(ctx, proto, mbjs_point_class_id);
    MBJS_FreeValue(ctx, proto);
    if (MBJS_IsException(obj))
        goto fail;
    MBJS_SetOpaque(obj, s);
    return obj;
 fail:
    mbjs_free(ctx, s);
    MBJS_FreeValue(ctx, obj);
    return MBJS_EXCEPTION;
}

static MBJSValue mbjs_point_get_xy(MBJSContext *ctx, MBJSValue this_val, int magic)
{
    MBJSPointData *s = MBJS_GetOpaque2(ctx, this_val, mbjs_point_class_id);
    if (!s)
        return MBJS_EXCEPTION;
    if (magic == 0)
        return MBJS_NewInt32(ctx, s->x);
    else
        return MBJS_NewInt32(ctx, s->y);
}

static MBJSValue mbjs_point_set_xy(MBJSContext *ctx, MBJSValue this_val, MBJSValue val, int magic)
{
    MBJSPointData *s = MBJS_GetOpaque2(ctx, this_val, mbjs_point_class_id);
    int v;
    if (!s)
        return MBJS_EXCEPTION;
    if (MBJS_ToInt32(ctx, &v, val))
        return MBJS_EXCEPTION;
    if (magic == 0)
        s->x = v;
    else
        s->y = v;
    return MBJS_UNDEFINED;
}

static MBJSValue mbjs_point_norm(MBJSContext *ctx, MBJSValue this_val,
                             int argc, MBJSValue *argv)
{
    MBJSPointData *s = MBJS_GetOpaque2(ctx, this_val, mbjs_point_class_id);
    if (!s)
        return MBJS_EXCEPTION;
    return MBJS_NewFloat64(ctx, sqrt((double)s->x * s->x + (double)s->y * s->y));
}

static MBJSClassDef mbjs_point_class = {
    "Point",
    .finalizer = mbjs_point_finalizer,
};

static const MBJSCFunctionListEntry mbjs_point_proto_funcs[] = {
    MBJS_CGETSET_MAGIC_DEF("x", mbjs_point_get_xy, mbjs_point_set_xy, 0),
    MBJS_CGETSET_MAGIC_DEF("y", mbjs_point_get_xy, mbjs_point_set_xy, 1),
    MBJS_CFUNC_DEF("norm", 0, mbjs_point_norm),
};

static int mbjs_point_init(MBJSContext *ctx, MBJSModuleDef *m)
{
    MBJSValue point_proto, point_class;
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);

    /* create the Point class */
    MBJS_NewClassID(rt, &mbjs_point_class_id);
    MBJS_NewClass(rt, mbjs_point_class_id, &mbjs_point_class);

    point_proto = MBJS_NewObject(ctx);
    MBJS_SetPropertyFunctionList(ctx, point_proto, mbjs_point_proto_funcs, countof(mbjs_point_proto_funcs));

    point_class = MBJS_NewCFunction2(ctx, mbjs_point_ctor, "Point", 2, MBJS_CFUNC_constructor, 0);
    /* set proto.constructor and ctor.prototype */
    MBJS_SetConstructor(ctx, point_class, point_proto);
    MBJS_SetClassProto(ctx, mbjs_point_class_id, point_proto);

    MBJS_SetModuleExport(ctx, m, "Point", point_class);
    return 0;
}

MBJS_MODULE_EXTERN MBJSModuleDef *mbjs_init_module(MBJSContext *ctx, const char *module_name)
{
    MBJSModuleDef *m;
    m = MBJS_NewCModule(ctx, module_name, mbjs_point_init);
    if (!m)
        return NULL;
    MBJS_AddModuleExport(ctx, m, "Point");
    return m;
}
