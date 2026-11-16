/*
 * mbjs Javascript Engine
 *
 * Copyright (c) 2017-2026 Fabrice Bellard
 * Copyright (c) 2017-2024 Charlie Gordon
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
#ifndef mbjs_H
#define mbjs_H

/* ------------------------------------------------------------------ */
/* Package identity (reverse-domain)                                   */
/* ------------------------------------------------------------------ */

/* The packaged artifact is "libmochiberry" (libmochiberry.so / .dll / .a /
 * .o) with the reverse-domain package name my.berrylabfoundation.mochiberry.
 * Every build tool (CMake, pkg-config, the OTA packager) and the host
 * API use these constants so the identity is defined once. */
#define MBJS_PACKAGE_NAME "my.berrylabfoundation.mochiberry"
#define MBJS_PACKAGE_ORG "my.berrylabfoundation"
#define MBJS_PACKAGE_ARTIFACT "mochiberry"
#define MBJS_PACKAGE_LIBRARY "libmochiberry"

/* Language-level feature target: the engine + the pure-JS patches under
 * patches/ (patch_ecma2025.js, patch_ecma2026.js, ...) together provide the
 * ECMA-262 2026 candidate feature set. Syntax-only proposals are delivered
 * by the engine; the rest are installed at boot by patches/patch_loader.js. */
#define MBJS_ECMA_LEVEL 2026


#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MBJS_NG 1

/* Helpers. */
#if defined(_WIN32) || defined(__CYGWIN__)
# define MBJS_NG_PLAT_WIN32 1
#endif /* defined(_WIN32) || defined(__CYGWIN__) */

#if defined(__GNUC__) || defined(__clang__)
# define MBJS_NG_CC_GNULIKE 1
#endif /* defined(__GNUC__) || defined(__clang__) */

/*
 * `MBJS_EXTERN` -- helper macro that must be used to mark the external
 * interfaces of libmbjs.
 *
 * Define BUILDING_MBJS_SHARED when building and USING_MBJS_SHARED when using
 * shared libmbjs.
 *
 * Windows note: The `__declspec` syntax is supported by both Clang and GCC.
 * If building mbjs, the BUILDING_MBJS_SHARED macro must be defined for libmbjs
 * (and only for it) to properly export symbols.
 */
#ifdef MBJS_NG_PLAT_WIN32
# if defined(BUILDING_MBJS_SHARED)
#  define MBJS_EXTERN __declspec(dllexport)
# elif defined(USING_MBJS_SHARED)
#  define MBJS_EXTERN __declspec(dllimport)
# else
#  define MBJS_EXTERN /* nothing */
# endif
#else
# if defined(BUILDING_MBJS_SHARED) && defined(MBJS_NG_CC_GNULIKE)
#  define MBJS_EXTERN __attribute__((visibility("default")))
# else
#  define MBJS_EXTERN /* nothing */
# endif
#endif /* MBJS_NG_PLAT_WIN32 */

/* Returns MBJS_PACKAGE_NAME ("my.berrylabfoundation.mochiberry"). */
MBJS_EXTERN const char *MBJS_GetPackageName(void);

/*
 * `MBJS_LIBC_EXTERN` -- helper macro that must be used to mark the extern
 * interfaces of mbjs-libc specifically.
 */
#if defined(MBJS_NG_BUILD) && !defined(MBJS_BUILD_LIBC) && defined(MBJS_NG_PLAT_WIN32)
/*
 * We are building mbjs-NG, mbjs-libc is a static library and we are on
 * Windows. Then, make sure to not export any interfaces.
 */
# define MBJS_LIBC_EXTERN /* nothing */
#else
/*
 * Otherwise, if we are either (1) not building mbjs-NG, (2) libc is built as
 * a part of libmbjs, or (3) we are not on Windows, define MBJS_LIBC_EXTERN to
 * MBJS_EXTERN.
 */
# define MBJS_LIBC_EXTERN MBJS_EXTERN
#endif

/*
 * `MBJS_MODULE_EXTERN` -- helper macro that must be used to mark `mbjs_init_module`
 * and other public functions of the binary modules. See examples/ for examples
 * of the usage.
 *
 * Windows note: -DMBJS_NG_MODULE_BUILD must be set when building a binary
 * module to properly set __declspec.
 */
#ifdef MBJS_NG_PLAT_WIN32
# ifdef MBJS_NG_MODULE_BUILD
#  define MBJS_MODULE_EXTERN __declspec(dllexport)
# else
#  define MBJS_MODULE_EXTERN __declspec(dllimport)
# endif
#else
# if defined(MBJS_NG_MODULE_BUILD) && defined(MBJS_NG_CC_GNULIKE)
#  define MBJS_MODULE_EXTERN __attribute__((visibility("default")))
# else
#  define MBJS_MODULE_EXTERN /* nothing */
# endif
#endif /* MBJS_NG_PLAT_WIN32 */

/* Borrowed from Folly */
#ifndef MBJS_PRINTF_FORMAT
/* Clang on Windows doesn't seem to support _Printf_format_string_ */
#if defined(_MSC_VER) && !defined(__clang__)
#include <sal.h>
#define MBJS_PRINTF_FORMAT _Printf_format_string_
#define MBJS_PRINTF_FORMAT_ATTR(format_param, dots_param)
#else
#define MBJS_PRINTF_FORMAT
#if !defined(__clang__) && defined(__GNUC__)
#define MBJS_PRINTF_FORMAT_ATTR(format_param, dots_param) \
  __attribute__((format(gnu_printf, format_param, dots_param)))
#else
#define MBJS_PRINTF_FORMAT_ATTR(format_param, dots_param) \
  __attribute__((format(printf, format_param, dots_param)))
#endif
#endif
#endif

/*
 * `MBJS_CALL_X` -- macro tree for calling a function with a variable number of arguments.
 * This is used for bulk operations such as freeing values or atoms.
 */
#define MBJS_COUNT_ARGS_(_0, _8, _7, _6, _5, _4, _3, _2, _1, N, ...) N
#define MBJS_COUNT_ARGS(...) MBJS_COUNT_ARGS_(0, __VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)

#define MBJS_CALLX1(F,X,a)               do { F(X,a); } while (0)
#define MBJS_CALLX2(F,X,a,b)             do { F(X,a); F(X,b); } while (0)
#define MBJS_CALLX3(F,X,a,b,c)           do { F(X,a); F(X,b); F(X,c); } while (0)
#define MBJS_CALLX4(F,X,a,b,c,d)         do { F(X,a); F(X,b); F(X,c); F(X,d); } while (0)
#define MBJS_CALLX5(F,X,a,b,c,d,e)       do { F(X,a); F(X,b); F(X,c); F(X,d); F(X,e); } while (0)
#define MBJS_CALLX6(F,X,a,b,c,d,e,f)     do { F(X,a); F(X,b); F(X,c); F(X,d); F(X,e); F(X,f); } while (0)
#define MBJS_CALLX7(F,X,a,b,c,d,e,f,g)   do { F(X,a); F(X,b); F(X,c); F(X,d); F(X,e); F(X,f); F(X,g); } while (0)
#define MBJS_CALLX8(F,X,a,b,c,d,e,f,g,h) do { F(X,a); F(X,b); F(X,c); F(X,d); F(X,e); F(X,f); F(X,g); F(X,h); } while (0)

#define MBJS_CALLX__(F, X, N, ...) MBJS_CALLX##N(F, X, __VA_ARGS__)
#define MBJS_CALLX_(F, X, N, ...) MBJS_CALLX__(F, X, N, __VA_ARGS__)

#undef MBJS_NG_CC_GNULIKE
#undef MBJS_NG_PLAT_WIN32

typedef struct MBJSRuntime MBJSRuntime;
typedef struct MBJSContext MBJSContext;
typedef struct MBJSObject MBJSObject;
typedef uint32_t MBJSClassID;
typedef uint32_t MBJSAtom;

/* Unless documented otherwise, C string pointers (`char *` or `const char *`)
   are assumed to verify these constraints:
   - unless a length is passed separately, the string has a null terminator
   - string contents is either pure ASCII or is UTF-8 encoded.
 */

/* Overridable purely for testing purposes; don't touch. */
#ifndef MBJS_NAN_BOXING
#if INTPTR_MAX < INT64_MAX
#define MBJS_NAN_BOXING 1 /* Use NAN boxing for 32bit builds. */
#endif
#endif

enum {
    /* all tags with a reference count are negative */
    MBJS_TAG_FIRST       = -9, /* first negative tag */
    MBJS_TAG_BIG_INT     = -9,
    MBJS_TAG_SYMBOL      = -8,
    MBJS_TAG_STRING      = -7,
    MBJS_TAG_STRING_ROPE = -6,
    MBJS_TAG_MODULE      = -3, /* used internally */
    MBJS_TAG_FUNCTION_BYTECODE = -2, /* used internally */
    MBJS_TAG_OBJECT      = -1,

    MBJS_TAG_INT         = 0,
    MBJS_TAG_BOOL        = 1,
    MBJS_TAG_NULL        = 2,
    MBJS_TAG_UNDEFINED   = 3,
    MBJS_TAG_UNINITIALIZED = 4,
    MBJS_TAG_CATCH_OFFSET = 5,
    MBJS_TAG_EXCEPTION   = 6,
    MBJS_TAG_SHORT_BIG_INT = 7,
    MBJS_TAG_FLOAT64     = 8,
    /* any larger tag is FLOAT64 if MBJS_NAN_BOXING */
};

#if !defined(MBJS_CHECK_JSVALUE)
#define MBJSValueConst MBJSValue
#endif

// MBJS_CHECK_JSVALUE build mode does not produce working code but is here to
// help catch reference counting bugs at compile time, by making it harder
// to mix up MBJSValue and MBJSValueConst
//
// rules:
//
// - a function with a MBJSValue parameter takes ownership;
//   caller must *not* call MBJS_FreeValue
//
// - a function with a MBJSValueConst parameter does not take ownership;
//   caller *must* call MBJS_FreeValue
//
// - a function returning a MBJSValue transfers ownership to caller;
//   caller *must* call MBJS_FreeValue
//
// - a function returning a MBJSValueConst does *not* transfer ownership;
//   caller must *not* call MBJS_FreeValue
#if defined(MBJS_CHECK_JSVALUE)

typedef struct MBJSValue *MBJSValue;
typedef const struct MBJSValue *MBJSValueConst;

#define MBJS_MKVAL(tag, val)       ((MBJSValue)((tag) | (intptr_t)(val) << 4))
#define MBJS_MKPTR(tag, ptr)       ((MBJSValue)((tag) | (intptr_t)(ptr)))
#define MBJS_VALUE_GET_NORM_TAG(v) ((int)((intptr_t)(v) & 15))
#define MBJS_VALUE_GET_TAG(v)      ((int)((intptr_t)(v) & 15))
#define MBJS_VALUE_GET_SHORT_BIG_INT(v) MBJS_VALUE_GET_INT(v)
#define MBJS_VALUE_GET_PTR(v)      ((void *)((intptr_t)(v) & ~15))
#define MBJS_VALUE_GET_INT(v)      ((int)((intptr_t)(v) >> 4))
#define MBJS_VALUE_GET_BOOL(v)     ((int)((intptr_t)(v) >> 4))
#define MBJS_VALUE_GET_FLOAT64(v)  ((double)((intptr_t)(v) >> 4))
#define MBJS_TAG_IS_FLOAT64(tag)   ((int)(tag) == MBJS_TAG_FLOAT64)
#define MBJS_NAN                   MBJS_MKVAL(MBJS_TAG_FLOAT64, 0)

static inline MBJSValue __JS_NewFloat64(double d)
{
    return MBJS_MKVAL(MBJS_TAG_FLOAT64, (int)d);
}

static inline bool MBJS_VALUE_IS_NAN(MBJSValue v)
{
    (void)&v;
    return false;
}

#elif defined(MBJS_NAN_BOXING) && MBJS_NAN_BOXING

typedef uint64_t MBJSValue;

#define MBJS_VALUE_GET_TAG(v) (int)((v) >> 32)
#define MBJS_VALUE_GET_INT(v) (int)(v)
#define MBJS_VALUE_GET_BOOL(v) (int)(v)
#define MBJS_VALUE_GET_SHORT_BIG_INT(v) (int)(v)
#define MBJS_VALUE_GET_PTR(v) (void *)(intptr_t)(v)

#define MBJS_MKVAL(tag, val) (((uint64_t)(tag) << 32) | (uint32_t)(val))
#define MBJS_MKPTR(tag, ptr) (((uint64_t)(tag) << 32) | (uintptr_t)(ptr))

#define MBJS_FLOAT64_TAG_ADDEND (0x7ff80000 - MBJS_TAG_FIRST + 1) /* quiet NaN encoding */

static inline double MBJS_VALUE_GET_FLOAT64(MBJSValue v)
{
    union {
        MBJSValue v;
        double d;
    } u;
    u.v = v;
    u.v += (uint64_t)MBJS_FLOAT64_TAG_ADDEND << 32;
    return u.d;
}

#define MBJS_NAN (0x7ff8000000000000 - ((uint64_t)MBJS_FLOAT64_TAG_ADDEND << 32))

static inline MBJSValue __JS_NewFloat64(double d)
{
    union {
        double d;
        uint64_t u64;
    } u;
    MBJSValue v;
    u.d = d;
    /* normalize NaN */
    if ((u.u64 & 0x7fffffffffffffff) > 0x7ff0000000000000)
        v = MBJS_NAN;
    else
        v = u.u64 - ((uint64_t)MBJS_FLOAT64_TAG_ADDEND << 32);
    return v;
}

#define MBJS_TAG_IS_FLOAT64(tag) ((unsigned)((tag) - MBJS_TAG_FIRST) >= (MBJS_TAG_FLOAT64 - MBJS_TAG_FIRST))

/* same as MBJS_VALUE_GET_TAG, but return MBJS_TAG_FLOAT64 with NaN boxing */
static inline int MBJS_VALUE_GET_NORM_TAG(MBJSValue v)
{
    uint32_t tag;
    tag = MBJS_VALUE_GET_TAG(v);
    if (MBJS_TAG_IS_FLOAT64(tag))
        return MBJS_TAG_FLOAT64;
    else
        return tag;
}

static inline bool MBJS_VALUE_IS_NAN(MBJSValue v)
{
    uint32_t tag;
    tag = MBJS_VALUE_GET_TAG(v);
    return tag == (MBJS_NAN >> 32);
}

#else /* !MBJS_NAN_BOXING */

typedef union MBJSValueUnion {
    /* MBJS_MKVAL() writes all 64 bits even though only 32 are needed:
       leaving the upper half uninitialized makes GCC 14+ generate much
       slower code on x86-64 */
    uint64_t uint64;
    double float64;
    void *ptr;
    int32_t short_big_int;
} MBJSValueUnion;

typedef struct MBJSValue {
    MBJSValueUnion u;
    int64_t tag;
} MBJSValue;

#define MBJS_VALUE_GET_TAG(v) ((int32_t)(v).tag)
/* same as MBJS_VALUE_GET_TAG, but return MBJS_TAG_FLOAT64 with NaN boxing */
#define MBJS_VALUE_GET_NORM_TAG(v) MBJS_VALUE_GET_TAG(v)
#define MBJS_VALUE_GET_INT(v) ((int)(v).u.uint64)
#define MBJS_VALUE_GET_BOOL(v) ((int)(v).u.uint64)
#define MBJS_VALUE_GET_FLOAT64(v) ((v).u.float64)
#define MBJS_VALUE_GET_SHORT_BIG_INT(v) ((int32_t)(v).u.uint64)
#define MBJS_VALUE_GET_PTR(v) ((v).u.ptr)

/* msvc doesn't understand designated initializers without /std:c++20 */
#ifdef __cplusplus
static inline MBJSValue MBJS_MKPTR(int64_t tag, void *ptr)
{
    MBJSValue v;
    v.u.ptr = ptr;
    v.tag = tag;
    return v;
}
static inline MBJSValue MBJS_MKVAL(int64_t tag, int32_t int32)
{
    MBJSValue v;
    v.u.uint64 = (uint32_t)int32;
    v.tag = tag;
    return v;
}
static inline MBJSValue MBJS_MKNAN(void)
{
    MBJSValue v;
    v.u.float64 = NAN;
    v.tag = MBJS_TAG_FLOAT64;
    return v;
}
/* provide as macros for consistency and backward compat reasons */
#define MBJS_MKPTR(tag, ptr) MBJS_MKPTR(tag, ptr)
#define MBJS_MKVAL(tag, val) MBJS_MKVAL(tag, val)
#define MBJS_NAN             MBJS_MKNAN() /* alas, not a constant expression */
#else
#define MBJS_MKPTR(tag, p)   (MBJSValue){ (MBJSValueUnion){ .ptr = p }, tag }
#define MBJS_MKVAL(tag, val) (MBJSValue){ (MBJSValueUnion){ .uint64 = (uint32_t)(val) }, tag }
#define MBJS_NAN             (MBJSValue){ (MBJSValueUnion){ .float64 = NAN }, MBJS_TAG_FLOAT64 }
#endif

#define MBJS_TAG_IS_FLOAT64(tag) ((unsigned)(tag) == MBJS_TAG_FLOAT64)

static inline MBJSValue __JS_NewFloat64(double d)
{
    MBJSValue v;
    v.tag = MBJS_TAG_FLOAT64;
    v.u.float64 = d;
    return v;
}

static inline bool MBJS_VALUE_IS_NAN(MBJSValue v)
{
    union {
        double d;
        uint64_t u64;
    } u;
    if (v.tag != MBJS_TAG_FLOAT64)
        return 0;
    u.d = v.u.float64;
    return (u.u64 & 0x7fffffffffffffff) > 0x7ff0000000000000;
}

#endif /* !MBJS_NAN_BOXING */

#define MBJS_VALUE_IS_BOTH_INT(v1, v2) ((MBJS_VALUE_GET_TAG(v1) | MBJS_VALUE_GET_TAG(v2)) == 0)
#define MBJS_VALUE_IS_BOTH_FLOAT(v1, v2) (MBJS_TAG_IS_FLOAT64(MBJS_VALUE_GET_TAG(v1)) && MBJS_TAG_IS_FLOAT64(MBJS_VALUE_GET_TAG(v2)))

#define MBJS_VALUE_HAS_REF_COUNT(v) ((unsigned)MBJS_VALUE_GET_TAG(v) >= (unsigned)MBJS_TAG_FIRST)

/* special values */
#define MBJS_NULL      MBJS_MKVAL(MBJS_TAG_NULL, 0)
#define MBJS_UNDEFINED MBJS_MKVAL(MBJS_TAG_UNDEFINED, 0)
#define MBJS_FALSE     MBJS_MKVAL(MBJS_TAG_BOOL, 0)
#define MBJS_TRUE      MBJS_MKVAL(MBJS_TAG_BOOL, 1)
#define MBJS_EXCEPTION MBJS_MKVAL(MBJS_TAG_EXCEPTION, 0)
#define MBJS_UNINITIALIZED MBJS_MKVAL(MBJS_TAG_UNINITIALIZED, 0)

/* flags for object properties */
#define MBJS_PROP_CONFIGURABLE  (1 << 0)
#define MBJS_PROP_WRITABLE      (1 << 1)
#define MBJS_PROP_ENUMERABLE    (1 << 2)
#define MBJS_PROP_C_W_E         (MBJS_PROP_CONFIGURABLE | MBJS_PROP_WRITABLE | MBJS_PROP_ENUMERABLE)
#define MBJS_PROP_LENGTH        (1 << 3) /* used internally in Arrays */
#define MBJS_PROP_TMASK         (3 << 4) /* mask for NORMAL, GETSET, VARREF, AUTOINIT */
#define MBJS_PROP_NORMAL         (0 << 4)
#define MBJS_PROP_GETSET         (1 << 4)
#define MBJS_PROP_VARREF         (2 << 4) /* used internally */
#define MBJS_PROP_AUTOINIT       (3 << 4) /* used internally */

/* flags for MBJS_DefineProperty */
#define MBJS_PROP_HAS_SHIFT        8
#define MBJS_PROP_HAS_CONFIGURABLE (1 << 8)
#define MBJS_PROP_HAS_WRITABLE     (1 << 9)
#define MBJS_PROP_HAS_ENUMERABLE   (1 << 10)
#define MBJS_PROP_HAS_GET          (1 << 11)
#define MBJS_PROP_HAS_SET          (1 << 12)
#define MBJS_PROP_HAS_VALUE        (1 << 13)

/* throw an exception if false would be returned
   (MBJS_DefineProperty/MBJS_SetProperty) */
#define MBJS_PROP_THROW            (1 << 14)
/* throw an exception if false would be returned in strict mode
   (MBJS_SetProperty) */
#define MBJS_PROP_THROW_STRICT     (1 << 15)

#define MBJS_PROP_NO_ADD           (1 << 16) /* internal use */
#define MBJS_PROP_NO_EXOTIC        (1 << 17) /* internal use */
#define MBJS_PROP_DEFINE_PROPERTY  (1 << 18) /* internal use */
#define MBJS_PROP_REFLECT_DEFINE_PROPERTY (1 << 19) /* internal use */

#ifndef MBJS_DEFAULT_STACK_SIZE
#define MBJS_DEFAULT_STACK_SIZE (1024 * 1024)
#endif

/* MBJS_Eval() flags */
#define MBJS_EVAL_TYPE_GLOBAL   (0 << 0) /* global code (default) */
#define MBJS_EVAL_TYPE_MODULE   (1 << 0) /* module code */
#define MBJS_EVAL_TYPE_DIRECT   (2 << 0) /* direct call (internal use) */
#define MBJS_EVAL_TYPE_INDIRECT (3 << 0) /* indirect call (internal use) */
#define MBJS_EVAL_TYPE_MASK     (3 << 0)

#define MBJS_EVAL_FLAG_STRICT   (1 << 3) /* force 'strict' mode */
#define MBJS_EVAL_FLAG_UNUSED   (1 << 4) /* unused */
/* compile but do not run. The result is an object with a
   MBJS_TAG_FUNCTION_BYTECODE or MBJS_TAG_MODULE tag. It can be executed
   with MBJS_EvalFunction(). */
#define MBJS_EVAL_FLAG_COMPILE_ONLY (1 << 5)
/* don't include the stack frames before this eval in the Error() backtraces */
#define MBJS_EVAL_FLAG_BACKTRACE_BARRIER (1 << 6)
/* allow top-level await in normal script. MBJS_Eval() returns a
   promise. Only allowed with MBJS_EVAL_TYPE_GLOBAL */
#define MBJS_EVAL_FLAG_ASYNC (1 << 7)

typedef MBJSValue MBJSCFunction(MBJSContext *ctx, MBJSValueConst this_val, int argc, MBJSValueConst *argv);
typedef MBJSValue MBJSCFunctionMagic(MBJSContext *ctx, MBJSValueConst this_val, int argc, MBJSValueConst *argv, int magic);
typedef MBJSValue MBJSCFunctionData(MBJSContext *ctx, MBJSValueConst this_val, int argc, MBJSValueConst *argv, int magic, MBJSValueConst *func_data);
typedef MBJSValue MBJSCClosure(MBJSContext *ctx, MBJSValueConst this_val, int argc, MBJSValueConst *argv, int magic, void *opaque);

typedef struct MBJSMallocFunctions {
    void *(*mbjs_calloc)(void *opaque, size_t count, size_t size);
    void *(*mbjs_malloc)(void *opaque, size_t size);
    void (*mbjs_free)(void *opaque, void *ptr);
    void *(*mbjs_realloc)(void *opaque, void *ptr, size_t size);
    size_t (*mbjs_malloc_usable_size)(const void *ptr);
} MBJSMallocFunctions;

// Debug trace system: the debug output will be produced to the dump stream (currently
// stdout) if dumps are enabled and MBJS_SetDumpFlags is invoked with the corresponding
// bit set.
#define MBJS_DUMP_BYTECODE_FINAL   0x01  /* dump pass 3 final byte code */
#define MBJS_DUMP_BYTECODE_PASS2   0x02  /* dump pass 2 code */
#define MBJS_DUMP_BYTECODE_PASS1   0x04  /* dump pass 1 code */
#define MBJS_DUMP_BYTECODE_HEX     0x10  /* dump bytecode in hex */
#define MBJS_DUMP_BYTECODE_PC2LINE 0x20  /* dump line number table */
#define MBJS_DUMP_BYTECODE_STACK   0x40  /* dump compute_stack_size */
#define MBJS_DUMP_BYTECODE_STEP    0x80  /* dump executed bytecode */
#define MBJS_DUMP_READ_OBJECT     0x100  /* dump the marshalled objects at load time */
#define MBJS_DUMP_FREE            0x200  /* dump every object free */
#define MBJS_DUMP_GC              0x400  /* dump the occurrence of the automatic GC */
#define MBJS_DUMP_GC_FREE         0x800  /* dump objects freed by the GC */
#define MBJS_DUMP_MODULE_RESOLVE 0x1000  /* dump module resolution steps */
#define MBJS_DUMP_PROMISE        0x2000  /* dump promise steps */
#define MBJS_DUMP_LEAKS          0x4000  /* dump leaked objects and strings in MBJS_FreeRuntime */
#define MBJS_DUMP_ATOM_LEAKS     0x8000  /* dump leaked atoms in MBJS_FreeRuntime */
#define MBJS_DUMP_MEM           0x10000  /* dump memory usage in MBJS_FreeRuntime */
#define MBJS_DUMP_OBJECTS       0x20000  /* dump objects in MBJS_FreeRuntime */
#define MBJS_DUMP_ATOMS         0x40000  /* dump atoms in MBJS_FreeRuntime */
#define MBJS_DUMP_SHAPES        0x80000  /* dump shapes in MBJS_FreeRuntime */
#define MBJS_ABORT_ON_LEAKS    0x10C000  /* abort on atom/object/string leaks; for testing */

// Finalizers run in LIFO order at the very end of MBJS_FreeRuntime.
// Intended for cleanup of associated resources; the runtime itself
// is no longer usable.
typedef void MBJSRuntimeFinalizer(MBJSRuntime *rt, void *arg);

typedef struct MBJSGCObjectHeader MBJSGCObjectHeader;

MBJS_EXTERN MBJSRuntime *MBJS_NewRuntime(void);
/* info lifetime must exceed that of rt */
MBJS_EXTERN void MBJS_SetRuntimeInfo(MBJSRuntime *rt, const char *info);
/* use 0 to disable memory limit */
MBJS_EXTERN void MBJS_SetMemoryLimit(MBJSRuntime *rt, size_t limit);
MBJS_EXTERN void MBJS_SetDumpFlags(MBJSRuntime *rt, uint64_t flags);
MBJS_EXTERN uint64_t MBJS_GetDumpFlags(MBJSRuntime *rt);
MBJS_EXTERN size_t MBJS_GetGCThreshold(MBJSRuntime *rt);
MBJS_EXTERN void MBJS_SetGCThreshold(MBJSRuntime *rt, size_t gc_threshold);
/* use 0 to disable maximum stack size check */
MBJS_EXTERN void MBJS_SetMaxStackSize(MBJSRuntime *rt, size_t stack_size);
/* should be called when changing thread to update the stack top value
   used to check stack overflow. */
MBJS_EXTERN void MBJS_UpdateStackTop(MBJSRuntime *rt);
MBJS_EXTERN MBJSRuntime *MBJS_NewRuntime2(const MBJSMallocFunctions *mf, void *opaque);
MBJS_EXTERN void MBJS_FreeRuntime(MBJSRuntime *rt);
MBJS_EXTERN void *MBJS_GetRuntimeOpaque(MBJSRuntime *rt);
MBJS_EXTERN void MBJS_SetRuntimeOpaque(MBJSRuntime *rt, void *opaque);
MBJS_EXTERN int MBJS_AddRuntimeFinalizer(MBJSRuntime *rt,
                                     MBJSRuntimeFinalizer *finalizer, void *arg);
typedef void MBJS_MarkFunc(MBJSRuntime *rt, MBJSGCObjectHeader *gp);
MBJS_EXTERN void MBJS_MarkValue(MBJSRuntime *rt, MBJSValueConst val,
                            MBJS_MarkFunc *mark_func);
MBJS_EXTERN void MBJS_RunGC(MBJSRuntime *rt);
MBJS_EXTERN bool MBJS_IsLiveObject(MBJSRuntime *rt, MBJSValueConst obj);

#ifdef MBJS_SUNAT_GC
/* Sunat GC build: the garbage collector is circumcised out of the runtime
   and every allocation lives in one deterministic mbjs-mem arena that is
   reclaimed in bulk at teardown. These helpers are compiled into the sunat
   build only (link against dist/sunat/libmochiberry.so). */
typedef struct MBJSMemSunatStats {
    size_t gc_runs;             /* always 0: the GC is circumcised */
    size_t arena_used;          /* bytes handed out by the backing arena */
    size_t arena_peak;          /* high-water mark of arena_used */
    size_t arena_limit;         /* hard budget (0 = unlimited) */
    size_t engine_malloc_size;  /* engine-accounted bytes */
    size_t engine_malloc_count; /* engine-accounted allocations */
} MBJSMemSunatStats;

MBJS_EXTERN void MBJS_GetSunatStats(MBJSRuntime *rt, MBJSMemSunatStats *st);
/* The mbjs-mem arena backing the runtime, or NULL. For runtimes created
   through MBJS_NewRuntime() the runtime owns the arena and destroys it at
   MBJS_FreeRuntime(). For runtimes created through
   MBJS_NewRuntime2(mbjs_mem_mf(), arena) this returns the embedder-owned
   arena, which the embedder keeps and must destroy. */
MBJS_EXTERN void *MBJS_GetSunatArena(MBJSRuntime *rt);
#endif /* MBJS_SUNAT_GC */

MBJS_EXTERN MBJSContext *MBJS_NewContext(MBJSRuntime *rt);
MBJS_EXTERN void MBJS_FreeContext(MBJSContext *s);
MBJS_EXTERN MBJSContext *MBJS_DupContext(MBJSContext *ctx);
MBJS_EXTERN void *MBJS_GetContextOpaque(MBJSContext *ctx);
MBJS_EXTERN void MBJS_SetContextOpaque(MBJSContext *ctx, void *opaque);
MBJS_EXTERN MBJSRuntime *MBJS_GetRuntime(MBJSContext *ctx);
MBJS_EXTERN void MBJS_SetClassProto(MBJSContext *ctx, MBJSClassID class_id, MBJSValue obj);
MBJS_EXTERN MBJSValue MBJS_GetClassProto(MBJSContext *ctx, MBJSClassID class_id);
MBJS_EXTERN MBJSValue MBJS_GetFunctionProto(MBJSContext *ctx);

/* the following functions are used to select the intrinsic object to
   save memory */
MBJS_EXTERN MBJSContext *MBJS_NewContextRaw(MBJSRuntime *rt);
MBJS_EXTERN int MBJS_AddIntrinsicBaseObjects(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddIntrinsicDate(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddIntrinsicEval(MBJSContext *ctx);
MBJS_EXTERN void MBJS_AddIntrinsicRegExpCompiler(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddIntrinsicRegExp(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddIntrinsicJSON(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddIntrinsicProxy(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddIntrinsicMapSet(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddIntrinsicTypedArrays(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddIntrinsicPromise(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddIntrinsicBigInt(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddIntrinsicWeakRef(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddPerformance(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddIntrinsicDOMException(MBJSContext *ctx);
MBJS_EXTERN int MBJS_AddIntrinsicAToB(MBJSContext *ctx);

/* for equality comparisons and sameness */
MBJS_EXTERN int MBJS_IsEqual(MBJSContext *ctx, MBJSValueConst op1, MBJSValueConst op2);
MBJS_EXTERN bool MBJS_IsStrictEqual(MBJSContext *ctx, MBJSValueConst op1, MBJSValueConst op2);
MBJS_EXTERN bool MBJS_IsSameValue(MBJSContext *ctx, MBJSValueConst op1, MBJSValueConst op2);
/* Similar to same-value equality, but +0 and -0 are considered equal. */
MBJS_EXTERN bool MBJS_IsSameValueZero(MBJSContext *ctx, MBJSValueConst op1, MBJSValueConst op2);

/* Only used for running 262 tests. TODO(saghul) add build time flag. */
MBJS_EXTERN MBJSValue mbjs_string_codePointRange(MBJSContext *ctx, MBJSValueConst this_val,
                                           int argc, MBJSValueConst *argv);

MBJS_EXTERN void *mbjs_calloc_rt(MBJSRuntime *rt, size_t count, size_t size);
MBJS_EXTERN void *mbjs_malloc_rt(MBJSRuntime *rt, size_t size);
MBJS_EXTERN void mbjs_free_rt(MBJSRuntime *rt, void *ptr);
MBJS_EXTERN void *mbjs_realloc_rt(MBJSRuntime *rt, void *ptr, size_t size);
MBJS_EXTERN size_t mbjs_malloc_usable_size_rt(MBJSRuntime *rt, const void *ptr);
MBJS_EXTERN void *mbjs_mallocz_rt(MBJSRuntime *rt, size_t size);

MBJS_EXTERN void *mbjs_calloc(MBJSContext *ctx, size_t count, size_t size);
MBJS_EXTERN void *mbjs_malloc(MBJSContext *ctx, size_t size);
MBJS_EXTERN void mbjs_free(MBJSContext *ctx, void *ptr);
MBJS_EXTERN void *mbjs_realloc(MBJSContext *ctx, void *ptr, size_t size);
MBJS_EXTERN size_t mbjs_malloc_usable_size(MBJSContext *ctx, const void *ptr);
MBJS_EXTERN void *mbjs_mallocz(MBJSContext *ctx, size_t size);
MBJS_EXTERN char *mbjs_strdup(MBJSContext *ctx, const char *str);
MBJS_EXTERN char *mbjs_strndup(MBJSContext *ctx, const char *s, size_t n);

typedef struct MBJSMemoryUsage {
    int64_t malloc_size, malloc_limit, memory_used_size;
    int64_t malloc_count;
    int64_t memory_used_count;
    int64_t atom_count, atom_size;
    int64_t str_count, str_size;
    int64_t obj_count, obj_size;
    int64_t prop_count, prop_size;
    int64_t shape_count, shape_size;
    int64_t mbjs_func_count, mbjs_func_size, mbjs_func_code_size;
    int64_t mbjs_func_pc2line_count, mbjs_func_pc2line_size;
    int64_t c_func_count, array_count;
    int64_t fast_array_count, fast_array_elements;
    int64_t binary_object_count, binary_object_size;
} MBJSMemoryUsage;

MBJS_EXTERN void MBJS_ComputeMemoryUsage(MBJSRuntime *rt, MBJSMemoryUsage *s);
MBJS_EXTERN void MBJS_DumpMemoryUsage(FILE *fp, const MBJSMemoryUsage *s, MBJSRuntime *rt);

/* atom support */
#define MBJS_ATOM_NULL 0

MBJS_EXTERN MBJSAtom MBJS_NewAtomLen(MBJSContext *ctx, const char *str, size_t len);
MBJS_EXTERN MBJSAtom MBJS_NewAtom(MBJSContext *ctx, const char *str);
MBJS_EXTERN MBJSAtom MBJS_NewAtomUInt32(MBJSContext *ctx, uint32_t n);
MBJS_EXTERN MBJSAtom MBJS_DupAtom(MBJSContext *ctx, MBJSAtom v);
MBJS_EXTERN MBJSAtom MBJS_DupAtomRT(MBJSRuntime *rt, MBJSAtom v);
MBJS_EXTERN void MBJS_FreeAtom(MBJSContext *ctx, MBJSAtom v);
MBJS_EXTERN void MBJS_FreeAtomRT(MBJSRuntime *rt, MBJSAtom v);
#define MBJS_FreeAtoms(ctx, ...) MBJS_CALLX_(MBJS_FreeAtom, ctx, MBJS_COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)
#define MBJS_FreeAtomsRT(rt, ...) MBJS_CALLX_(MBJS_FreeAtomRT, rt, MBJS_COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)
MBJS_EXTERN MBJSValue MBJS_AtomToValue(MBJSContext *ctx, MBJSAtom atom);
MBJS_EXTERN MBJSValue MBJS_AtomToString(MBJSContext *ctx, MBJSAtom atom);
MBJS_EXTERN const char *MBJS_AtomToCStringLen(MBJSContext *ctx, size_t *plen, MBJSAtom atom);
static inline const char *MBJS_AtomToCString(MBJSContext *ctx, MBJSAtom atom)
{
    return MBJS_AtomToCStringLen(ctx, NULL, atom);
}
MBJS_EXTERN MBJSAtom MBJS_ValueToAtom(MBJSContext *ctx, MBJSValueConst val);

/* object class support */

typedef struct MBJSPropertyEnum {
    bool is_enumerable;
    MBJSAtom atom;
} MBJSPropertyEnum;

typedef struct MBJSPropertyDescriptor {
    int flags;
    MBJSValue value;
    MBJSValue getter;
    MBJSValue setter;
} MBJSPropertyDescriptor;

typedef struct MBJSClassExoticMethods {
    /* Return -1 if exception (can only happen in case of Proxy object),
       false if the property does not exists, true if it exists. If 1 is
       returned, the property descriptor 'desc' is filled if != NULL. */
    int (*get_own_property)(MBJSContext *ctx, MBJSPropertyDescriptor *desc,
                             MBJSValueConst obj, MBJSAtom prop);
    /* '*ptab' should hold the '*plen' property keys. Return 0 if OK,
       -1 if exception. The 'is_enumerable' field is ignored.
    */
    int (*get_own_property_names)(MBJSContext *ctx, MBJSPropertyEnum **ptab,
                                  uint32_t *plen, MBJSValueConst obj);
    /* return < 0 if exception, or true/false */
    int (*delete_property)(MBJSContext *ctx, MBJSValueConst obj, MBJSAtom prop);
    /* return < 0 if exception or true/false */
    int (*define_own_property)(MBJSContext *ctx, MBJSValueConst this_obj,
                               MBJSAtom prop, MBJSValueConst val,
                               MBJSValueConst getter, MBJSValueConst setter,
                               int flags);
    /* The following methods can be emulated with the previous ones,
       so they are usually not needed */
    /* return < 0 if exception or true/false */
    int (*has_property)(MBJSContext *ctx, MBJSValueConst obj, MBJSAtom atom);
    MBJSValue (*get_property)(MBJSContext *ctx, MBJSValueConst obj, MBJSAtom atom,
                            MBJSValueConst receiver);
    /* return < 0 if exception or true/false */
    int (*set_property)(MBJSContext *ctx, MBJSValueConst obj, MBJSAtom atom,
                        MBJSValueConst value, MBJSValueConst receiver, int flags);
} MBJSClassExoticMethods;

typedef void MBJSClassFinalizer(MBJSRuntime *rt, MBJSValueConst val);
typedef void MBJSClassGCMark(MBJSRuntime *rt, MBJSValueConst val,
                           MBJS_MarkFunc *mark_func);
#define MBJS_CALL_FLAG_CONSTRUCTOR (1 << 0)
typedef MBJSValue MBJSClassCall(MBJSContext *ctx, MBJSValueConst func_obj,
                            MBJSValueConst this_val, int argc,
                            MBJSValueConst *argv, int flags);

typedef struct MBJSClassDef {
    const char *class_name; /* pure ASCII only! */
    MBJSClassFinalizer *finalizer;
    MBJSClassGCMark *gc_mark;
    /* if call != NULL, the object is a function. If (flags &
       MBJS_CALL_FLAG_CONSTRUCTOR) != 0, the function is called as a
       constructor. In this case, 'this_val' is new.target. A
       constructor call only happens if the object constructor bit is
       set (see MBJS_SetConstructorBit()). */
    MBJSClassCall *call;
    /* XXX: suppress this indirection ? It is here only to save memory
       because only a few classes need these methods */
    MBJSClassExoticMethods *exotic;
} MBJSClassDef;

#define MBJS_EVAL_OPTIONS_VERSION 1

typedef struct MBJSEvalOptions {
  int version;
  int eval_flags;
  const char *filename;
  int line_num;
  // can add new fields in ABI-compatible manner by incrementing MBJS_EVAL_OPTIONS_VERSION
} MBJSEvalOptions;

#define MBJS_INVALID_CLASS_ID 0
MBJS_EXTERN MBJSClassID MBJS_NewClassID(MBJSRuntime *rt, MBJSClassID *pclass_id);
/* Returns the class ID if `v` is an object, otherwise returns MBJS_INVALID_CLASS_ID. */
MBJS_EXTERN MBJSClassID MBJS_GetClassID(MBJSValueConst v);
MBJS_EXTERN int MBJS_NewClass(MBJSRuntime *rt, MBJSClassID class_id, const MBJSClassDef *class_def);
MBJS_EXTERN bool MBJS_IsRegisteredClass(MBJSRuntime *rt, MBJSClassID class_id);
/* Returns the class name or MBJS_ATOM_NULL if `id` is not a registered class. Must be freed with MBJS_FreeAtom. */
MBJS_EXTERN MBJSAtom MBJS_GetClassName(MBJSRuntime *rt, MBJSClassID class_id);

/* value handling */

static inline MBJSValue MBJS_NewBool(MBJSContext *ctx, bool val)
{
    (void)&ctx;
    return MBJS_MKVAL(MBJS_TAG_BOOL, (val != 0));
}

static inline MBJSValue MBJS_NewInt32(MBJSContext *ctx, int32_t val)
{
    (void)&ctx;
    return MBJS_MKVAL(MBJS_TAG_INT, val);
}

static inline MBJSValue MBJS_NewFloat64(MBJSContext *ctx, double val)
{
    (void)&ctx;
    return __JS_NewFloat64(val);
}

static inline MBJSValue MBJS_NewCatchOffset(MBJSContext *ctx, int32_t val)
{
    (void)&ctx;
    return MBJS_MKVAL(MBJS_TAG_CATCH_OFFSET, val);
}

static inline MBJSValue MBJS_NewInt64(MBJSContext *ctx, int64_t val)
{
    MBJSValue v;
    if (val >= INT32_MIN && val <= INT32_MAX) {
        v = MBJS_NewInt32(ctx, (int32_t)val);
    } else {
        v = MBJS_NewFloat64(ctx, (double)val);
    }
    return v;
}

static inline MBJSValue MBJS_NewUint32(MBJSContext *ctx, uint32_t val)
{
    MBJSValue v;
    if (val <= INT32_MAX) {
        v = MBJS_NewInt32(ctx, (int32_t)val);
    } else {
        v = MBJS_NewFloat64(ctx, (double)val);
    }
    return v;
}

static inline MBJSValue MBJS_NewUint64(MBJSContext *ctx, uint64_t val)
{
    MBJSValue v;
    if (val <= INT32_MAX) {
        v = MBJS_NewInt32(ctx, (int32_t)val);
    } else {
        v = MBJS_NewFloat64(ctx, (double)val);
    }
    return v;
}

MBJS_EXTERN MBJSValue MBJS_NewNumber(MBJSContext *ctx, double d);
MBJS_EXTERN MBJSValue MBJS_NewBigInt64(MBJSContext *ctx, int64_t v);
MBJS_EXTERN MBJSValue MBJS_NewBigUint64(MBJSContext *ctx, uint64_t v);

static inline bool MBJS_IsNumber(MBJSValueConst v)
{
    int tag = MBJS_VALUE_GET_TAG(v);
    return tag == MBJS_TAG_INT || MBJS_TAG_IS_FLOAT64(tag);
}

static inline bool MBJS_IsBigInt(MBJSValueConst v)
{
    int tag = MBJS_VALUE_GET_TAG(v);
    return tag == MBJS_TAG_BIG_INT || tag == MBJS_TAG_SHORT_BIG_INT;
}

static inline bool MBJS_IsBool(MBJSValueConst v)
{
    return MBJS_VALUE_GET_TAG(v) == MBJS_TAG_BOOL;
}

static inline bool MBJS_IsNull(MBJSValueConst v)
{
    return MBJS_VALUE_GET_TAG(v) == MBJS_TAG_NULL;
}

static inline bool MBJS_IsUndefined(MBJSValueConst v)
{
    return MBJS_VALUE_GET_TAG(v) == MBJS_TAG_UNDEFINED;
}

static inline bool MBJS_IsException(MBJSValueConst v)
{
    return MBJS_VALUE_GET_TAG(v) == MBJS_TAG_EXCEPTION;
}

static inline bool MBJS_IsUninitialized(MBJSValueConst v)
{
    return MBJS_VALUE_GET_TAG(v) == MBJS_TAG_UNINITIALIZED;
}

static inline bool MBJS_IsString(MBJSValueConst v)
{
    int tag = MBJS_VALUE_GET_TAG(v);
    return tag == MBJS_TAG_STRING || tag == MBJS_TAG_STRING_ROPE;
}

static inline bool MBJS_IsSymbol(MBJSValueConst v)
{
    return MBJS_VALUE_GET_TAG(v) == MBJS_TAG_SYMBOL;
}

static inline bool MBJS_IsObject(MBJSValueConst v)
{
    return MBJS_VALUE_GET_TAG(v) == MBJS_TAG_OBJECT;
}

static inline bool MBJS_IsModule(MBJSValueConst v)
{
    return MBJS_VALUE_GET_TAG(v) == MBJS_TAG_MODULE;
}

MBJS_EXTERN MBJSValue MBJS_Throw(MBJSContext *ctx, MBJSValue obj);
MBJS_EXTERN MBJSValue MBJS_GetException(MBJSContext *ctx);
MBJS_EXTERN bool MBJS_HasException(MBJSContext *ctx);
MBJS_EXTERN bool MBJS_IsError(MBJSValueConst val);
MBJS_EXTERN bool MBJS_IsUncatchableError(MBJSValueConst val);
MBJS_EXTERN void MBJS_SetUncatchableError(MBJSContext *ctx, MBJSValueConst val);
MBJS_EXTERN void MBJS_ClearUncatchableError(MBJSContext *ctx, MBJSValueConst val);
// Shorthand for:
//  MBJSValue exc = MBJS_GetException(ctx);
//  MBJS_ClearUncatchableError(ctx, exc);
//  MBJS_Throw(ctx, exc);
MBJS_EXTERN void MBJS_ResetUncatchableError(MBJSContext *ctx);
MBJS_EXTERN MBJSValue MBJS_NewError(MBJSContext *ctx);
// InternalError cannot be caught by JS code
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(2, 3) MBJS_NewInternalError(MBJSContext *ctx, MBJS_PRINTF_FORMAT const char *fmt, ...);
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(2, 3) MBJS_NewPlainError(MBJSContext *ctx, MBJS_PRINTF_FORMAT const char *fmt, ...);
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(2, 3) MBJS_NewRangeError(MBJSContext *ctx, MBJS_PRINTF_FORMAT const char *fmt, ...);
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(2, 3) MBJS_NewReferenceError(MBJSContext *ctx, MBJS_PRINTF_FORMAT const char *fmt, ...);
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(2, 3) MBJS_NewSyntaxError(MBJSContext *ctx, MBJS_PRINTF_FORMAT const char *fmt, ...);
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(2, 3) MBJS_NewTypeError(MBJSContext *ctx, MBJS_PRINTF_FORMAT const char *fmt, ...);
// InternalError cannot be caught by JS code
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(2, 3) MBJS_ThrowInternalError(MBJSContext *ctx, MBJS_PRINTF_FORMAT const char *fmt, ...);
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(2, 3) MBJS_ThrowPlainError(MBJSContext *ctx, MBJS_PRINTF_FORMAT const char *fmt, ...);
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(2, 3) MBJS_ThrowRangeError(MBJSContext *ctx, MBJS_PRINTF_FORMAT const char *fmt, ...);
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(2, 3) MBJS_ThrowReferenceError(MBJSContext *ctx, MBJS_PRINTF_FORMAT const char *fmt, ...);
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(2, 3) MBJS_ThrowSyntaxError(MBJSContext *ctx, MBJS_PRINTF_FORMAT const char *fmt, ...);
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(2, 3) MBJS_ThrowTypeError(MBJSContext *ctx, MBJS_PRINTF_FORMAT const char *fmt, ...);
MBJS_EXTERN MBJSValue MBJS_PRINTF_FORMAT_ATTR(3, 4) MBJS_ThrowDOMException(MBJSContext *ctx, const char *name, MBJS_PRINTF_FORMAT const char *fmt, ...);
MBJS_EXTERN MBJSValue MBJS_ThrowOutOfMemory(MBJSContext *ctx);
MBJS_EXTERN void MBJS_FreeValue(MBJSContext *ctx, MBJSValue v);
MBJS_EXTERN void MBJS_FreeValueRT(MBJSRuntime *rt, MBJSValue v);
#define MBJS_FreeValues(ctx, ...) MBJS_CALLX_(MBJS_FreeValue, ctx, MBJS_COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)
#define MBJS_FreeValuesRT(rt, ...) MBJS_CALLX_(MBJS_FreeValueRT, rt, MBJS_COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)
MBJS_EXTERN MBJSValue MBJS_DupValue(MBJSContext *ctx, MBJSValueConst v);
MBJS_EXTERN MBJSValue MBJS_DupValueRT(MBJSRuntime *rt, MBJSValueConst v);
MBJS_EXTERN int MBJS_ToBool(MBJSContext *ctx, MBJSValueConst val); /* return -1 for MBJS_EXCEPTION */
static inline MBJSValue MBJS_ToBoolean(MBJSContext *ctx, MBJSValueConst val)
{
    return MBJS_NewBool(ctx, MBJS_ToBool(ctx, val));
}
MBJS_EXTERN MBJSValue MBJS_ToNumber(MBJSContext *ctx, MBJSValueConst val);
MBJS_EXTERN int MBJS_ToInt32(MBJSContext *ctx, int32_t *pres, MBJSValueConst val);
static inline int MBJS_ToUint32(MBJSContext *ctx, uint32_t *pres, MBJSValueConst val)
{
    return MBJS_ToInt32(ctx, (int32_t*)pres, val);
}
MBJS_EXTERN int MBJS_ToInt64(MBJSContext *ctx, int64_t *pres, MBJSValueConst val);
MBJS_EXTERN int MBJS_ToIndex(MBJSContext *ctx, uint64_t *plen, MBJSValueConst val);
MBJS_EXTERN int MBJS_ToFloat64(MBJSContext *ctx, double *pres, MBJSValueConst val);
/* return an exception if 'val' is a Number */
MBJS_EXTERN int MBJS_ToBigInt64(MBJSContext *ctx, int64_t *pres, MBJSValueConst val);
MBJS_EXTERN int MBJS_ToBigUint64(MBJSContext *ctx, uint64_t *pres, MBJSValueConst val);
/* same as MBJS_ToInt64() but allow BigInt */
MBJS_EXTERN int MBJS_ToInt64Ext(MBJSContext *ctx, int64_t *pres, MBJSValueConst val);

MBJS_EXTERN MBJSValue MBJS_NewStringLen(MBJSContext *ctx, const char *str1, size_t len1);
static inline MBJSValue MBJS_NewString(MBJSContext *ctx, const char *str) {
    return MBJS_NewStringLen(ctx, str, strlen(str));
}
// makes a copy of the input; does not check if the input is valid UTF-16,
// that is the responsibility of the caller
MBJS_EXTERN MBJSValue MBJS_NewStringUTF16(MBJSContext *ctx, const uint16_t *buf,
                                    size_t len);
MBJS_EXTERN MBJSValue MBJS_NewAtomString(MBJSContext *ctx, const char *str);
MBJS_EXTERN MBJSValue MBJS_ToString(MBJSContext *ctx, MBJSValueConst val);
MBJS_EXTERN MBJSValue MBJS_ToPropertyKey(MBJSContext *ctx, MBJSValueConst val);
MBJS_EXTERN const char *MBJS_ToCStringLen2(MBJSContext *ctx, size_t *plen, MBJSValueConst val1, bool cesu8);
static inline const char *MBJS_ToCStringLen(MBJSContext *ctx, size_t *plen, MBJSValueConst val1)
{
    return MBJS_ToCStringLen2(ctx, plen, val1, 0);
}
static inline const char *MBJS_ToCString(MBJSContext *ctx, MBJSValueConst val1)
{
    return MBJS_ToCStringLen2(ctx, NULL, val1, 0);
}
// returns a utf-16 version of the string in native endianness; the
// string is not nul terminated and can contain unmatched surrogates
// |*plen| is in uint16s, not code points; a surrogate pair such as
// U+D834 U+DF06 has len=2; an unmatched surrogate has len=1
MBJS_EXTERN const uint16_t *MBJS_ToCStringLenUTF16(MBJSContext *ctx, size_t *plen,
                                               MBJSValueConst val1);
static inline const uint16_t *MBJS_ToCStringUTF16(MBJSContext *ctx,
                                                MBJSValueConst val1)
{
    return MBJS_ToCStringLenUTF16(ctx, NULL, val1);
}
MBJS_EXTERN void MBJS_FreeCString(MBJSContext *ctx, const char *ptr);
MBJS_EXTERN void MBJS_FreeCStringRT(MBJSRuntime *rt, const char *ptr);
MBJS_EXTERN void MBJS_FreeCStringUTF16(MBJSContext *ctx, const uint16_t *ptr);
MBJS_EXTERN void MBJS_FreeCStringRT_UTF16(MBJSRuntime *rt, const uint16_t *ptr);

MBJS_EXTERN MBJSValue MBJS_NewObjectProtoClass(MBJSContext *ctx, MBJSValueConst proto,
                                         MBJSClassID class_id);
MBJS_EXTERN MBJSValue MBJS_NewObjectClass(MBJSContext *ctx, MBJSClassID class_id);
MBJS_EXTERN MBJSValue MBJS_NewObjectProto(MBJSContext *ctx, MBJSValueConst proto);
MBJS_EXTERN MBJSValue MBJS_NewObject(MBJSContext *ctx);
// takes ownership of the values
MBJS_EXTERN MBJSValue MBJS_NewObjectFrom(MBJSContext *ctx, int count,
                                   const MBJSAtom *props,
                                   const MBJSValue *values);
// takes ownership of the values
MBJS_EXTERN MBJSValue MBJS_NewObjectFromStr(MBJSContext *ctx, int count,
                                      const char **props,
                                      const MBJSValue *values);
MBJS_EXTERN MBJSValue MBJS_ToObject(MBJSContext *ctx, MBJSValueConst val);
MBJS_EXTERN MBJSValue MBJS_ToObjectString(MBJSContext *ctx, MBJSValueConst val);

MBJS_EXTERN bool MBJS_IsFunction(MBJSContext* ctx, MBJSValueConst val);
MBJS_EXTERN bool MBJS_IsAsyncFunction(MBJSValueConst val);
MBJS_EXTERN bool MBJS_IsConstructor(MBJSContext* ctx, MBJSValueConst val);
MBJS_EXTERN int MBJS_GetFunctionBytecodeBuffer(MBJSValueConst val, const uint8_t **buf, size_t *plen);
MBJS_EXTERN int MBJS_GetFunctionConstantPool(MBJSValueConst val, const MBJSValue **pool, size_t *count);
MBJS_EXTERN bool MBJS_SetConstructorBit(MBJSContext *ctx, MBJSValueConst func_obj, bool val);

/* MochiAOT/LLVM accessor: frame layout of a compiled bytecode function.
 * `arg_count`/`var_count` size the argument and local slots, `stack_size`
 * is the maximum operand-stack depth, and `closure_vars` describes the
 * variables the function closes over: each entry says whether the captured
 * slot lives in the parent's locals (MBJS_CLOSURE_TYPE_LOCAL), in the
 * parent's arguments (MBJS_CLOSURE_TYPE_ARG) or in the parent's own
 * closure variables (MBJS_CLOSURE_TYPE_REF), plus the index in that parent
 * space. Any output pointer may be NULL. `closure_vars` must have room for
 * at least `*closure_var_count` entries; returns -1 if it does not (or if
 * `val` is not a bytecode function). */
#define MBJS_CLOSURE_TYPE_LOCAL       0
#define MBJS_CLOSURE_TYPE_ARG         1
#define MBJS_CLOSURE_TYPE_REF         2
#define MBJS_CLOSURE_TYPE_GLOBAL_REF  3
#define MBJS_CLOSURE_TYPE_GLOBAL_DECL 4
#define MBJS_CLOSURE_TYPE_GLOBAL      5
#define MBJS_CLOSURE_TYPE_MODULE_DECL 6
#define MBJS_CLOSURE_TYPE_MODULE_IMPORT 7

typedef struct MBJSClosureVarInfo {
    int closure_type;
    unsigned short var_idx;
} MBJSClosureVarInfo;

MBJS_EXTERN int MBJS_GetFunctionFrameInfo(MBJSValueConst val,
                                          unsigned short *arg_count,
                                          unsigned short *var_count,
                                          unsigned short *stack_size,
                                          unsigned short *closure_var_count,
                                          MBJSClosureVarInfo *closure_vars,
                                          unsigned short closure_vars_cap);

MBJS_EXTERN bool MBJS_IsRegExp(MBJSValueConst val);
MBJS_EXTERN bool MBJS_IsMap(MBJSValueConst val);
MBJS_EXTERN bool MBJS_IsSet(MBJSValueConst val);
MBJS_EXTERN bool MBJS_IsWeakRef(MBJSValueConst val);
MBJS_EXTERN bool MBJS_IsWeakSet(MBJSValueConst val);
MBJS_EXTERN bool MBJS_IsWeakMap(MBJSValueConst val);
MBJS_EXTERN bool MBJS_IsDataView(MBJSValueConst val);

MBJS_EXTERN MBJSValue MBJS_NewArray(MBJSContext *ctx);
// takes ownership of the values
MBJS_EXTERN MBJSValue MBJS_NewArrayFrom(MBJSContext *ctx, int count,
                                  const MBJSValue *values);
// reader beware: MBJS_IsArray used to "punch" through proxies and check
// if the target object is an array but it no longer does; use MBJS_IsProxy
// and MBJS_GetProxyTarget instead, and remember that the target itself can
// also be a proxy, ad infinitum
MBJS_EXTERN bool MBJS_IsArray(MBJSValueConst val);

MBJS_EXTERN bool MBJS_IsProxy(MBJSValueConst val);
MBJS_EXTERN MBJSValue MBJS_GetProxyTarget(MBJSContext *ctx, MBJSValueConst proxy);
MBJS_EXTERN MBJSValue MBJS_GetProxyHandler(MBJSContext *ctx, MBJSValueConst proxy);
MBJS_EXTERN MBJSValue MBJS_NewProxy(MBJSContext *ctx, MBJSValueConst target,
                              MBJSValueConst handler);

MBJS_EXTERN MBJSValue MBJS_NewDate(MBJSContext *ctx, double epoch_ms);
MBJS_EXTERN bool MBJS_IsDate(MBJSValueConst v);

MBJS_EXTERN MBJSValue MBJS_GetProperty(MBJSContext *ctx, MBJSValueConst this_obj, MBJSAtom prop);
MBJS_EXTERN MBJSValue MBJS_GetPropertyUint32(MBJSContext *ctx, MBJSValueConst this_obj,
                                       uint32_t idx);
MBJS_EXTERN MBJSValue MBJS_GetPropertyInt64(MBJSContext *ctx, MBJSValueConst this_obj,
                                      int64_t idx);
MBJS_EXTERN MBJSValue MBJS_GetPropertyStr(MBJSContext *ctx, MBJSValueConst this_obj,
                                    const char *prop);
MBJS_EXTERN MBJSValue MBJS_GetPropertyValue(MBJSContext *ctx, MBJSValueConst this_obj,
                                      MBJSValue prop);

MBJS_EXTERN int MBJS_SetProperty(MBJSContext *ctx, MBJSValueConst this_obj,
                             MBJSAtom prop, MBJSValue val);
MBJS_EXTERN int MBJS_SetPropertyUint32(MBJSContext *ctx, MBJSValueConst this_obj,
                                   uint32_t idx, MBJSValue val);
MBJS_EXTERN int MBJS_SetPropertyInt64(MBJSContext *ctx, MBJSValueConst this_obj,
                                  int64_t idx, MBJSValue val);
MBJS_EXTERN int MBJS_SetPropertyStr(MBJSContext *ctx, MBJSValueConst this_obj,
                                const char *prop, MBJSValue val);
// |flags| is a mix of MBJS_PROP_* values; when in doubt use MBJS_PROP_C_W_E
MBJS_EXTERN int MBJS_SetPropertyValue(MBJSContext *ctx, MBJSValueConst this_obj,
                                  MBJSValue prop, MBJSValue val, int flags);
MBJS_EXTERN int MBJS_HasProperty(MBJSContext *ctx, MBJSValueConst this_obj, MBJSAtom prop);
MBJS_EXTERN int MBJS_IsExtensible(MBJSContext *ctx, MBJSValueConst obj);
MBJS_EXTERN int MBJS_PreventExtensions(MBJSContext *ctx, MBJSValueConst obj);
MBJS_EXTERN int MBJS_DeleteProperty(MBJSContext *ctx, MBJSValueConst obj, MBJSAtom prop, int flags);
MBJS_EXTERN int MBJS_SetPrototype(MBJSContext *ctx, MBJSValueConst obj, MBJSValueConst proto_val);
MBJS_EXTERN MBJSValue MBJS_GetPrototype(MBJSContext *ctx, MBJSValueConst val);
MBJS_EXTERN int MBJS_GetLength(MBJSContext *ctx, MBJSValueConst obj, int64_t *pres);
MBJS_EXTERN int MBJS_SetLength(MBJSContext *ctx, MBJSValueConst obj, int64_t len);
MBJS_EXTERN int MBJS_SealObject(MBJSContext *ctx, MBJSValueConst obj);
MBJS_EXTERN int MBJS_FreezeObject(MBJSContext *ctx, MBJSValueConst obj);

#define MBJS_GPN_STRING_MASK  (1 << 0)
#define MBJS_GPN_SYMBOL_MASK  (1 << 1)
#define MBJS_GPN_PRIVATE_MASK (1 << 2)
/* only include the enumerable properties */
#define MBJS_GPN_ENUM_ONLY    (1 << 4)
/* set theJSPropertyEnum.is_enumerable field */
#define MBJS_GPN_SET_ENUM     (1 << 5)

MBJS_EXTERN int MBJS_GetOwnPropertyNames(MBJSContext *ctx, MBJSPropertyEnum **ptab,
                                     uint32_t *plen, MBJSValueConst obj,
                                     int flags);
MBJS_EXTERN int MBJS_GetOwnProperty(MBJSContext *ctx, MBJSPropertyDescriptor *desc,
                                MBJSValueConst obj, MBJSAtom prop);
MBJS_EXTERN void MBJS_FreePropertyEnum(MBJSContext *ctx, MBJSPropertyEnum *tab,
                                   uint32_t len);

MBJS_EXTERN MBJSValue MBJS_Call(MBJSContext *ctx, MBJSValueConst func_obj,
                          MBJSValueConst this_obj, int argc, MBJSValueConst *argv);
MBJS_EXTERN MBJSValue MBJS_Invoke(MBJSContext *ctx, MBJSValueConst this_val, MBJSAtom atom,
                            int argc, MBJSValueConst *argv);
MBJS_EXTERN MBJSValue MBJS_CallConstructor(MBJSContext *ctx, MBJSValueConst func_obj,
                                     int argc, MBJSValueConst *argv);
MBJS_EXTERN MBJSValue MBJS_CallConstructor2(MBJSContext *ctx, MBJSValueConst func_obj,
                                      MBJSValueConst new_target,
                                      int argc, MBJSValueConst *argv);
/* Try to detect if the input is a module. Returns true if parsing the input
 * as a module produces no syntax errors. It's a naive approach that is not
 * wholly infallible: non-strict classic scripts may _parse_ okay as a module
 * but not _execute_ as one (different runtime semantics.) Use with caution.
 * |input| can be either ASCII or UTF-8 encoded source code.
 * Returns false if mbjs was built with -DMBJS_DISABLE_PARSER.
 */
MBJS_EXTERN bool MBJS_DetectModule(const char *input, size_t input_len);
/* 'input' must be zero terminated i.e. input[input_len] = '\0'. */
MBJS_EXTERN MBJSValue MBJS_Eval(MBJSContext *ctx, const char *input, size_t input_len,
                          const char *filename, int eval_flags);
MBJS_EXTERN MBJSValue MBJS_Eval2(MBJSContext *ctx, const char *input, size_t input_len,
                           MBJSEvalOptions *options);
MBJS_EXTERN MBJSValue MBJS_EvalThis(MBJSContext *ctx, MBJSValueConst this_obj,
                              const char *input, size_t input_len,
                              const char *filename, int eval_flags);
MBJS_EXTERN MBJSValue MBJS_EvalThis2(MBJSContext *ctx, MBJSValueConst this_obj,
                              const char *input, size_t input_len,
                              MBJSEvalOptions *options);
MBJS_EXTERN MBJSValue MBJS_GetGlobalObject(MBJSContext *ctx);
MBJS_EXTERN int MBJS_IsInstanceOf(MBJSContext *ctx, MBJSValueConst val, MBJSValueConst obj);
MBJS_EXTERN int MBJS_DefineProperty(MBJSContext *ctx, MBJSValueConst this_obj,
                                MBJSAtom prop, MBJSValueConst val,
                                MBJSValueConst getter, MBJSValueConst setter,
                                int flags);
MBJS_EXTERN int MBJS_DefinePropertyValue(MBJSContext *ctx, MBJSValueConst this_obj,
                                     MBJSAtom prop, MBJSValue val, int flags);
MBJS_EXTERN int MBJS_DefinePropertyValueUint32(MBJSContext *ctx, MBJSValueConst this_obj,
                                           uint32_t idx, MBJSValue val, int flags);
MBJS_EXTERN int MBJS_DefinePropertyValueStr(MBJSContext *ctx, MBJSValueConst this_obj,
                                        const char *prop, MBJSValue val, int flags);
MBJS_EXTERN int MBJS_DefinePropertyGetSet(MBJSContext *ctx, MBJSValueConst this_obj,
                                      MBJSAtom prop, MBJSValue getter, MBJSValue setter,
                                      int flags);
/* Only supported for custom classes, returns 0 on success < 0 otherwise. */
MBJS_EXTERN int MBJS_SetOpaque(MBJSValueConst obj, void *opaque);
MBJS_EXTERN void *MBJS_GetOpaque(MBJSValueConst obj, MBJSClassID class_id);
MBJS_EXTERN void *MBJS_GetOpaque2(MBJSContext *ctx, MBJSValueConst obj, MBJSClassID class_id);
MBJS_EXTERN void *MBJS_GetAnyOpaque(MBJSValueConst obj, MBJSClassID *class_id);

/* 'buf' must be zero terminated i.e. buf[buf_len] = '\0'. */
MBJS_EXTERN MBJSValue MBJS_ParseJSON(MBJSContext *ctx, const char *buf, size_t buf_len,
                               const char *filename);
MBJS_EXTERN MBJSValue MBJS_JSONStringify(MBJSContext *ctx, MBJSValueConst obj,
                                   MBJSValueConst replacer, MBJSValueConst space0);

/* Manages the backing memory of an externally created ArrayBuffer.
   When 'size' is zero, 'ptr' must be freed and NULL returned. Otherwise the
   block must be resized to 'size' bytes and the new pointer returned, or NULL
   if that is not possible, indicating a memory error, in which case 'ptr' must
   stay valid and the ArrayBuffer keeps its current size. */
typedef void *MBJSReallocArrayBufferDataFunc(MBJSRuntime *rt, void *opaque,
                                           void *ptr, size_t size);
/* Creates an ArrayBuffer backed by 'buf'. Pass a zero 'max_len' for a regular,
   fixed length ArrayBuffer; a non-zero 'max_len' makes it resizable and is its
   maxByteLength. 'realloc_func' may be NULL if the memory must not be managed
   by mbjs at all; such an ArrayBuffer can neither be resized nor
   transferred to a different length. It is required for resizable
   ArrayBuffers, except for SharedArrayBuffers: those commit their maximum size
   upfront and therefore need 'buf' to be at least 'max_len' bytes big. */
MBJS_EXTERN MBJSValue MBJS_NewArrayBuffer(MBJSContext *ctx, uint8_t *buf, size_t len,
                                    size_t max_len,
                                    MBJSReallocArrayBufferDataFunc *realloc_func,
                                    void *opaque, bool is_shared);
MBJS_EXTERN MBJSValue MBJS_NewArrayBufferCopy(MBJSContext *ctx, const uint8_t *buf, size_t len);
MBJS_EXTERN void MBJS_DetachArrayBuffer(MBJSContext *ctx, MBJSValueConst obj);
MBJS_EXTERN uint8_t *MBJS_GetArrayBuffer(MBJSContext *ctx, size_t *psize, MBJSValueConst obj);
MBJS_EXTERN bool MBJS_IsArrayBuffer(MBJSValueConst obj);
// returns true or false if obj is an ArrayBuffer, -1 otherwise
MBJS_EXTERN int MBJS_IsImmutableArrayBuffer(MBJSValueConst obj);
// returns 0 if obj is an ArrayBuffer, -1 otherwise
MBJS_EXTERN int MBJS_SetImmutableArrayBuffer(MBJSValueConst obj, bool immutable);
MBJS_EXTERN uint8_t *MBJS_GetUint8Array(MBJSContext *ctx, size_t *psize, MBJSValueConst obj);

typedef enum MBJSTypedArrayEnum {
    MBJS_TYPED_ARRAY_UINT8C = 0,
    MBJS_TYPED_ARRAY_INT8,
    MBJS_TYPED_ARRAY_UINT8,
    MBJS_TYPED_ARRAY_INT16,
    MBJS_TYPED_ARRAY_UINT16,
    MBJS_TYPED_ARRAY_INT32,
    MBJS_TYPED_ARRAY_UINT32,
    MBJS_TYPED_ARRAY_BIG_INT64,
    MBJS_TYPED_ARRAY_BIG_UINT64,
    MBJS_TYPED_ARRAY_FLOAT16,
    MBJS_TYPED_ARRAY_FLOAT32,
    MBJS_TYPED_ARRAY_FLOAT64,
} MBJSTypedArrayEnum;

MBJS_EXTERN MBJSValue MBJS_NewTypedArray(MBJSContext *ctx, int argc, MBJSValueConst *argv,
                                   MBJSTypedArrayEnum array_type);
MBJS_EXTERN MBJSValue MBJS_GetTypedArrayBuffer(MBJSContext *ctx, MBJSValueConst obj,
                                         size_t *pbyte_offset,
                                         size_t *pbyte_length,
                                         size_t *pbytes_per_element);
MBJS_EXTERN MBJSValue MBJS_NewUint8Array(MBJSContext *ctx, uint8_t *buf, size_t len,
                                   MBJSReallocArrayBufferDataFunc *realloc_func,
                                   void *opaque, bool is_shared);
/* returns -1 if not a typed array otherwise return a MBJSTypedArrayEnum value */
MBJS_EXTERN int MBJS_GetTypedArrayType(MBJSValueConst obj);
MBJS_EXTERN MBJSValue MBJS_NewUint8ArrayCopy(MBJSContext *ctx, const uint8_t *buf, size_t len);
typedef struct {
    void *(*sab_alloc)(void *opaque, size_t size);
    void (*sab_free)(void *opaque, void *ptr);
    void (*sab_dup)(void *opaque, void *ptr);
    void *sab_opaque;
} MBJSSharedArrayBufferFunctions;
MBJS_EXTERN void MBJS_SetSharedArrayBufferFunctions(MBJSRuntime *rt, const MBJSSharedArrayBufferFunctions *sf);

typedef enum MBJSPromiseStateEnum {
    // argument to MBJS_PromiseState() was not in fact a promise
    MBJS_PROMISE_NOT_A_PROMISE = -1,
    MBJS_PROMISE_PENDING       =  0,
    MBJS_PROMISE_FULFILLED,
    MBJS_PROMISE_REJECTED,
} MBJSPromiseStateEnum;

MBJS_EXTERN MBJSValue MBJS_NewPromiseCapability(MBJSContext *ctx, MBJSValue *resolving_funcs);
MBJS_EXTERN MBJSValue MBJS_PromiseThen(MBJSContext *ctx, MBJSValueConst promise,
                                MBJSValueConst on_fulfilled,
                                MBJSValueConst on_rejected);
MBJS_EXTERN MBJSPromiseStateEnum MBJS_PromiseState(MBJSContext *ctx,
                                             MBJSValueConst promise);
MBJS_EXTERN MBJSValue MBJS_PromiseResult(MBJSContext *ctx, MBJSValueConst promise);
MBJS_EXTERN bool MBJS_IsPromise(MBJSValueConst val);
MBJS_EXTERN void MBJS_PromiseMarkAsHandled(MBJSContext *ctx, MBJSValueConst promise);
MBJS_EXTERN MBJSValue MBJS_NewSettledPromise(MBJSContext *ctx, bool is_reject, MBJSValueConst value);

// a symbol that is visible to JavaScript code
MBJS_EXTERN MBJSValue MBJS_NewSymbol(MBJSContext *ctx, const char *description, bool is_global);
// a symbol that is only visible to C code
MBJS_EXTERN MBJSValue MBJS_NewPrivateSymbol(MBJSContext *ctx, const char *description);

typedef enum MBJSPromiseHookType {
    MBJS_PROMISE_HOOK_INIT,     // emitted when a new promise is created
    MBJS_PROMISE_HOOK_BEFORE,   // runs right before promise.then is invoked
    MBJS_PROMISE_HOOK_AFTER,    // runs right after promise.then is invoked
    MBJS_PROMISE_HOOK_RESOLVE,  // not emitted for rejected promises
} MBJSPromiseHookType;

// parent_promise is only passed in when type == MBJS_PROMISE_HOOK_INIT and
// is then either a promise object or MBJS_UNDEFINED if the new promise does
// not have a parent promise; only promises created with promise.then have
// a parent promise
typedef void MBJSPromiseHook(MBJSContext *ctx, MBJSPromiseHookType type,
                           MBJSValueConst promise, MBJSValueConst parent_promise,
                           void *opaque);
MBJS_EXTERN void MBJS_SetPromiseHook(MBJSRuntime *rt, MBJSPromiseHook promise_hook,
                                 void *opaque);

/* is_handled = true means that the rejection is handled */
typedef void MBJSHostPromiseRejectionTracker(MBJSContext *ctx, MBJSValueConst promise,
                                           MBJSValueConst reason,
                                           bool is_handled, void *opaque);
MBJS_EXTERN void MBJS_SetHostPromiseRejectionTracker(MBJSRuntime *rt, MBJSHostPromiseRejectionTracker *cb, void *opaque);

/* return != 0 if the JS code needs to be interrupted */
typedef int MBJSInterruptHandler(MBJSContext *ctx, void *opaque);
MBJS_EXTERN void MBJS_SetInterruptHandler(MBJSRuntime *rt, MBJSInterruptHandler *cb, void *opaque);
/* if can_block is true, Atomics.wait() can be used */
MBJS_EXTERN void MBJS_SetCanBlock(MBJSRuntime *rt, bool can_block);
/* Superset selector (Takegawa TGPM). NULL/"default" = plain engine. */
MBJS_EXTERN int MBJS_SetSuperset(MBJSRuntime *rt, const char *name);
MBJS_EXTERN const char *MBJS_GetSuperset(MBJSRuntime *rt);
/* set the [IsHTMLDDA] internal slot */
MBJS_EXTERN void MBJS_SetIsHTMLDDA(MBJSContext *ctx, MBJSValueConst obj);

typedef struct MBJSModuleDef MBJSModuleDef;

/* return the module specifier (allocated with mbjs_malloc()) or NULL if
   exception */
typedef char *MBJSModuleNormalizeFunc(MBJSContext *ctx,
                                    const char *module_base_name,
                                    const char *module_name, void *opaque);
typedef char *MBJSModuleNormalizeFunc2(MBJSContext *ctx,
                                     const char *module_base_name,
                                     const char *module_name,
                                     MBJSValueConst attributes,
                                     void *opaque);
typedef MBJSModuleDef *MBJSModuleLoaderFunc(MBJSContext *ctx,
                                        const char *module_name, void *opaque);

// Dynamic module loading in detail:
//
// 1. JS code calls `import(url, {with: /*...*/})`
//
// 2. If there are import attributes, MBJSModuleCheckSupportedImportAttributes
//    runs synchronously (with the |opaque| passed to MBJS_SetModuleLoaderFunc2)
//
// 3. An unfulfilled promise is returned to the JS caller
//
// 4. A job is posted to the microtask queue that runs when something
//    calls MBJS_ExecutePendingJob or mbjs_std_await
//
// 5. The MBJSDynamicImportFunc callback is called
//
// 6. You, the embedder, load the resource at your convenience and eventually
//    call MBJS_LoadModule(). Pass the result to MBJS_FinishDynamicImport().
//
// Step 6 means the non-dynamic module loader needs to be aware of resources
// loaded by the dynamic module loader. How to implement that is up to you.
//
// Ownership of |token| passes to MBJSDynamicImportFunc. If you need to hold
// on to |basename|, |filename| or |attributes|, strdup or MBJS_DupValue them.
//
// You must *always* call MBJS_FinishDynamicImport(), even on error, otherwise
// the resources associated with |token| leak.
typedef void MBJSDynamicImportFunc(MBJSContext *ctx, const char *basename,
                                 const char *filename, MBJSValue token,
                                 MBJSValueConst attributes, void *opaque);

/* module loader with import attributes support */
typedef MBJSModuleDef *MBJSModuleLoaderFunc2(MBJSContext *ctx,
                                         const char *module_name, void *opaque,
                                         MBJSValueConst attributes);

/* return -1 if exception, 0 if OK */
typedef int MBJSModuleCheckSupportedImportAttributes(MBJSContext *ctx, void *opaque,
                                                   MBJSValueConst attributes);

/* module_normalize = NULL is allowed and invokes the default module
   filename normalizer */
MBJS_EXTERN void MBJS_SetModuleLoaderFunc(MBJSRuntime *rt,
                                      MBJSModuleNormalizeFunc *module_normalize,
                                      MBJSModuleLoaderFunc *module_loader, void *opaque);

/* same as MBJS_SetModuleLoaderFunc but with import attributes support */
MBJS_EXTERN void MBJS_SetModuleLoaderFunc2(MBJSRuntime *rt,
                                       MBJSModuleNormalizeFunc *module_normalize,
                                       MBJSModuleLoaderFunc2 *module_loader,
                                       MBJSModuleCheckSupportedImportAttributes *module_check_attrs,
                                       void *opaque);

MBJS_EXTERN void MBJS_SetDynamicImportFunc(MBJSRuntime *rt, MBJSDynamicImportFunc *func,
                                       void *opaque);

/* Set an attributes-aware module normalizer. Call after MBJS_SetModuleLoaderFunc2. */
MBJS_EXTERN void MBJS_SetModuleNormalizeFunc2(MBJSRuntime *rt,
                                          MBJSModuleNormalizeFunc2 *module_normalize);

// pass promise=MBJS_EXCEPTION and exception=<something> if an exception
// occurred during module loading; pass exception=MBJS_UNDEFINED otherwise
MBJS_EXTERN int MBJS_FinishDynamicImport(MBJSContext *ctx, MBJSValue token,
                                     MBJSValue promise, MBJSValue exception);

/* return the import.meta object of a module */
MBJS_EXTERN MBJSValue MBJS_GetImportMeta(MBJSContext *ctx, MBJSModuleDef *m);
MBJS_EXTERN MBJSAtom MBJS_GetModuleName(MBJSContext *ctx, MBJSModuleDef *m);
MBJS_EXTERN MBJSValue MBJS_GetModuleNamespace(MBJSContext *ctx, MBJSModuleDef *m);

/* associate a MBJSValue to a C module */
MBJS_EXTERN int MBJS_SetModulePrivateValue(MBJSContext *ctx, MBJSModuleDef *m, MBJSValue val);
MBJS_EXTERN MBJSValue MBJS_GetModulePrivateValue(MBJSContext *ctx, MBJSModuleDef *m);

/* JS Job support */

typedef MBJSValue MBJSJobFunc(MBJSContext *ctx, int argc, MBJSValueConst *argv);
MBJS_EXTERN int MBJS_EnqueueJob(MBJSContext *ctx, MBJSJobFunc *job_func,
                            int argc, MBJSValueConst *argv);

MBJS_EXTERN bool MBJS_IsJobPending(MBJSRuntime *rt);
MBJS_EXTERN MBJSContext *MBJS_GetPendingJobContext(MBJSRuntime *rt);
MBJS_EXTERN int MBJS_ExecutePendingJob(MBJSRuntime *rt, MBJSContext **pctx);

/* Structure to retrieve (de)serialized SharedArrayBuffer objects. */
typedef struct MBJSSABTab {
    uint8_t **tab;
    size_t len;
} MBJSSABTab;

/* Object Writer/Reader (currently only used to handle precompiled code) */
#define MBJS_WRITE_OBJ_BYTECODE  (1 << 0) /* allow function/module */
#define MBJS_WRITE_OBJ_BSWAP     (0)      /* byte swapped output (obsolete, handled transparently) */
#define MBJS_WRITE_OBJ_SAB       (1 << 2) /* allow SharedArrayBuffer */
#define MBJS_WRITE_OBJ_REFERENCE (1 << 3) /* allow object references to encode arbitrary object graph */
#define MBJS_WRITE_OBJ_STRIP_SOURCE  (1 << 4) /* do not write source code information */
#define MBJS_WRITE_OBJ_STRIP_DEBUG   (1 << 5) /* do not write debug information */
MBJS_EXTERN uint8_t *MBJS_WriteObject(MBJSContext *ctx, size_t *psize, MBJSValueConst obj, int flags);
MBJS_EXTERN uint8_t *MBJS_WriteObject2(MBJSContext *ctx, size_t *psize, MBJSValueConst obj,
                                   int flags, MBJSSABTab *psab_tab);

/* WARNING: only enable MBJS_READ_OBJ_BYTECODE on input from a trusted
   writer. The bytecode format is not designed to resist a hostile
   producer; loading adversarial bytecode can lead to memory corruption. */
#define MBJS_READ_OBJ_BYTECODE  (1 << 0) /* allow function/module */
#define MBJS_READ_OBJ_ROM_DATA  (0)      /* avoid duplicating 'buf' data (obsolete, broken by ICs) */
/* WARNING: serialized SharedArrayBuffers carry a literal host pointer in
   the blob; only enable MBJS_READ_OBJ_SAB on input produced by a trusted
   writer in the same process (e.g. another Worker on the same runtime). */
#define MBJS_READ_OBJ_SAB       (1 << 2) /* allow SharedArrayBuffer */
#define MBJS_READ_OBJ_REFERENCE (1 << 3) /* allow object references */
MBJS_EXTERN MBJSValue MBJS_ReadObject(MBJSContext *ctx, const uint8_t *buf, size_t buf_len, int flags);
MBJS_EXTERN MBJSValue MBJS_ReadObject2(MBJSContext *ctx, const uint8_t *buf, size_t buf_len,
                                 int flags, MBJSSABTab *psab_tab);
/* instantiate and evaluate a bytecode function. Only used when
   reading a script or module with MBJS_ReadObject() */
MBJS_EXTERN MBJSValue MBJS_EvalFunction(MBJSContext *ctx, MBJSValue fun_obj);
/* load the dependencies of the module 'obj'. Useful when MBJS_ReadObject()
   returns a module. */
MBJS_EXTERN int MBJS_ResolveModule(MBJSContext *ctx, MBJSValueConst obj);

/* only exported for os.Worker() */
MBJS_EXTERN MBJSAtom MBJS_GetScriptOrModuleName(MBJSContext *ctx, int n_stack_levels);
/* don't call this recursively */
MBJS_EXTERN MBJSValue MBJS_LoadModule(MBJSContext *ctx, const char *basename,
                                const char *filename, MBJSValueConst attributes);

/* C function definition */
typedef enum MBJSCFunctionEnum {  /* XXX: should rename for namespace isolation */
    MBJS_CFUNC_generic,
    MBJS_CFUNC_generic_magic,
    MBJS_CFUNC_constructor,
    MBJS_CFUNC_constructor_magic,
    MBJS_CFUNC_constructor_or_func,
    MBJS_CFUNC_constructor_or_func_magic,
    MBJS_CFUNC_f_f,
    MBJS_CFUNC_f_f_f,
    MBJS_CFUNC_getter,
    MBJS_CFUNC_setter,
    MBJS_CFUNC_getter_magic,
    MBJS_CFUNC_setter_magic,
    MBJS_CFUNC_iterator_next,
} MBJSCFunctionEnum;

typedef union MBJSCFunctionType {
    MBJSCFunction *generic;
    MBJSValue (*generic_magic)(MBJSContext *ctx, MBJSValueConst this_val, int argc, MBJSValueConst *argv, int magic);
    MBJSCFunction *constructor;
    MBJSValue (*constructor_magic)(MBJSContext *ctx, MBJSValueConst new_target, int argc, MBJSValueConst *argv, int magic);
    MBJSCFunction *constructor_or_func;
    double (*f_f)(double);
    double (*f_f_f)(double, double);
    MBJSValue (*getter)(MBJSContext *ctx, MBJSValueConst this_val);
    MBJSValue (*setter)(MBJSContext *ctx, MBJSValueConst this_val, MBJSValueConst val);
    MBJSValue (*getter_magic)(MBJSContext *ctx, MBJSValueConst this_val, int magic);
    MBJSValue (*setter_magic)(MBJSContext *ctx, MBJSValueConst this_val, MBJSValueConst val, int magic);
    MBJSValue (*iterator_next)(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv, int *pdone, int magic);
} MBJSCFunctionType;

MBJS_EXTERN MBJSValue MBJS_NewCFunction2(MBJSContext *ctx, MBJSCFunction *func,
                                   const char *name,
                                   int length, MBJSCFunctionEnum cproto, int magic);
MBJS_EXTERN MBJSValue MBJS_NewCFunction3(MBJSContext *ctx, MBJSCFunction *func,
                                   const char *name,
                                   int length, MBJSCFunctionEnum cproto, int magic,
                                   MBJSValueConst proto_val, int n_fields);
MBJS_EXTERN MBJSValue MBJS_NewCFunctionData(MBJSContext *ctx, MBJSCFunctionData *func,
                                      int length, int magic, int data_len,
                                      MBJSValueConst *data);
MBJS_EXTERN MBJSValue MBJS_NewCFunctionData2(MBJSContext *ctx, MBJSCFunctionData *func,
                                       const char *name,
                                       int length, int magic, int data_len,
                                       MBJSValueConst *data);
typedef void MBJSCClosureFinalizerFunc(void*);
MBJS_EXTERN MBJSValue MBJS_NewCClosure(MBJSContext *ctx, MBJSCClosure *func,
                                 const char *name,
                                 MBJSCClosureFinalizerFunc *opaque_finalize,
                                 int length, int magic, void *opaque);

static inline MBJSValue MBJS_NewCFunction(MBJSContext *ctx, MBJSCFunction *func,
                                      const char *name, int length)
{
    return MBJS_NewCFunction2(ctx, func, name, length, MBJS_CFUNC_generic, 0);
}

static inline MBJSValue MBJS_NewCFunctionMagic(MBJSContext *ctx, MBJSCFunctionMagic *func,
                                           const char *name, int length,
                                           MBJSCFunctionEnum cproto, int magic)
{
    /* Used to squelch a -Wcast-function-type warning. */
    MBJSCFunctionType ft;
    ft.generic_magic = func;
    return MBJS_NewCFunction2(ctx, ft.generic, name, length, cproto, magic);
}
MBJS_EXTERN int MBJS_SetConstructor(MBJSContext *ctx, MBJSValueConst func_obj,
                                MBJSValueConst proto);

/* C property definition */

typedef struct MBJSCFunctionListEntry {
    const char *name;       /* pure ASCII or UTF-8 encoded */
    uint8_t prop_flags;
    uint8_t def_type;
    int16_t magic;
    union {
        struct {
            uint8_t length; /* XXX: should move outside union */
            uint8_t cproto; /* XXX: should move outside union */
            MBJSCFunctionType cfunc;
        } func;
        struct {
            MBJSCFunctionType get;
            MBJSCFunctionType set;
        } getset;
        struct {
            const char *name;
            int base;
        } alias;
        struct {
            const struct MBJSCFunctionListEntry *tab;
            int len;
        } prop_list;
        const char *str;    /* pure ASCII or UTF-8 encoded */
        int32_t i32;
        int64_t i64;
        uint64_t u64;
        double f64;
    } u;
} MBJSCFunctionListEntry;

#define MBJS_DEF_CFUNC          0
#define MBJS_DEF_CGETSET        1
#define MBJS_DEF_CGETSET_MAGIC  2
#define MBJS_DEF_PROP_STRING    3
#define MBJS_DEF_PROP_INT32     4
#define MBJS_DEF_PROP_INT64     5
#define MBJS_DEF_PROP_DOUBLE    6
#define MBJS_DEF_PROP_UNDEFINED 7
#define MBJS_DEF_OBJECT         8
#define MBJS_DEF_ALIAS          9
#define MBJS_DEF_PROP_SYMBOL   10
#define MBJS_DEF_PROP_BOOL     11

/* Note: c++ does not like nested designators */
#define MBJS_CFUNC_DEF(name, length, func1) { name, MBJS_PROP_WRITABLE | MBJS_PROP_CONFIGURABLE, MBJS_DEF_CFUNC, 0, { .func = { length, MBJS_CFUNC_generic, { .generic = func1 } } } }
#define MBJS_CFUNC_DEF2(name, length, func1, prop_flags) { name, prop_flags, MBJS_DEF_CFUNC, 0, { .func = { length, MBJS_CFUNC_generic, { .generic = func1 } } } }
#define MBJS_CFUNC_MAGIC_DEF(name, length, func1, magic) { name, MBJS_PROP_WRITABLE | MBJS_PROP_CONFIGURABLE, MBJS_DEF_CFUNC, magic, { .func = { length, MBJS_CFUNC_generic_magic, { .generic_magic = func1 } } } }
#define MBJS_CFUNC_SPECIAL_DEF(name, length, cproto, func1) { name, MBJS_PROP_WRITABLE | MBJS_PROP_CONFIGURABLE, MBJS_DEF_CFUNC, 0, { .func = { length, MBJS_CFUNC_ ## cproto, { .cproto = func1 } } } }
#define MBJS_ITERATOR_NEXT_DEF(name, length, func1, magic) { name, MBJS_PROP_WRITABLE | MBJS_PROP_CONFIGURABLE, MBJS_DEF_CFUNC, magic, { .func = { length, MBJS_CFUNC_iterator_next, { .iterator_next = func1 } } } }
#define MBJS_CGETSET_DEF(name, fgetter, fsetter) { name, MBJS_PROP_CONFIGURABLE, MBJS_DEF_CGETSET, 0, { .getset = { .get = { .getter = fgetter }, .set = { .setter = fsetter } } } }
#define MBJS_CGETSET_DEF2(name, fgetter, fsetter, prop_flags) { name, prop_flags, MBJS_DEF_CGETSET, 0, { .getset = { .get = { .getter = fgetter }, .set = { .setter = fsetter } } } }
#define MBJS_CGETSET_MAGIC_DEF(name, fgetter, fsetter, magic) { name, MBJS_PROP_CONFIGURABLE, MBJS_DEF_CGETSET_MAGIC, magic, { .getset = { .get = { .getter_magic = fgetter }, .set = { .setter_magic = fsetter } } } }
#define MBJS_CGETSET_MAGIC_DEF2(name, fgetter, fsetter, magic, prop_flags) { name, prop_flags, MBJS_DEF_CGETSET_MAGIC, magic, { .getset = { .get = { .getter_magic = fgetter }, .set = { .setter_magic = fsetter } } } }
#define MBJS_PROP_STRING_DEF(name, cstr, prop_flags) { name, prop_flags, MBJS_DEF_PROP_STRING, 0, { .str = cstr } }
#define MBJS_PROP_INT32_DEF(name, val, prop_flags) { name, prop_flags, MBJS_DEF_PROP_INT32, 0, { .i32 = val } }
#define MBJS_PROP_INT64_DEF(name, val, prop_flags) { name, prop_flags, MBJS_DEF_PROP_INT64, 0, { .i64 = val } }
#define MBJS_PROP_DOUBLE_DEF(name, val, prop_flags) { name, prop_flags, MBJS_DEF_PROP_DOUBLE, 0, { .f64 = val } }
#define MBJS_PROP_U2D_DEF(name, val, prop_flags) { name, prop_flags, MBJS_DEF_PROP_DOUBLE, 0, { .u64 = val } }
#define MBJS_PROP_UNDEFINED_DEF(name, prop_flags) { name, prop_flags, MBJS_DEF_PROP_UNDEFINED, 0, { .i32 = 0 } }
#define MBJS_PROP_SYMBOL_DEF(name, val, prop_flags) { name, prop_flags, MBJS_DEF_PROP_SYMBOL, 0, { .i32 = val } }
#define MBJS_PROP_BOOL_DEF(name, val, prop_flags) { name, prop_flags, MBJS_DEF_PROP_BOOL, 0, { .i32 = val } }
#define MBJS_OBJECT_DEF(name, tab, len, prop_flags) { name, prop_flags, MBJS_DEF_OBJECT, 0, { .prop_list = { tab, len } } }
#define MBJS_ALIAS_DEF(name, from) { name, MBJS_PROP_WRITABLE | MBJS_PROP_CONFIGURABLE, MBJS_DEF_ALIAS, 0, { .alias = { from, -1 } } }
#define MBJS_ALIAS_BASE_DEF(name, from, base) { name, MBJS_PROP_WRITABLE | MBJS_PROP_CONFIGURABLE, MBJS_DEF_ALIAS, 0, { .alias = { from, base } } }

MBJS_EXTERN int MBJS_SetPropertyFunctionList(MBJSContext *ctx, MBJSValueConst obj,
                                          const MBJSCFunctionListEntry *tab,
                                          int len);

/* C module definition */

typedef int MBJSModuleInitFunc(MBJSContext *ctx, MBJSModuleDef *m);

MBJS_EXTERN MBJSModuleDef *MBJS_NewCModule(MBJSContext *ctx, const char *name_str,
                                     MBJSModuleInitFunc *func);
/* can only be called before the module is instantiated */
MBJS_EXTERN int MBJS_AddModuleExport(MBJSContext *ctx, MBJSModuleDef *m, const char *name_str);
MBJS_EXTERN int MBJS_AddModuleExportList(MBJSContext *ctx, MBJSModuleDef *m,
                                      const MBJSCFunctionListEntry *tab, int len);
/* can only be called after the module is instantiated */
MBJS_EXTERN int MBJS_SetModuleExport(MBJSContext *ctx, MBJSModuleDef *m, const char *export_name,
                                 MBJSValue val);
MBJS_EXTERN int MBJS_SetModuleExportList(MBJSContext *ctx, MBJSModuleDef *m,
                                     const MBJSCFunctionListEntry *tab, int len);

/* Version — single source of truth (mbjs-version.h). */

#include "mbjs-version.h"

MBJS_EXTERN const char* MBJS_GetVersion(void);
/* base provenance (name+version, and take date) come from mbjs-version.h. */
MBJS_EXTERN const char* MBJS_GetBaseVersion(void);
MBJS_EXTERN const char* MBJS_GetBaseDate(void);

/* Integration point for mbjs-libc.c, not for public use. */
MBJS_EXTERN uintptr_t mbjs_std_cmd(int cmd, ...);

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* mbjs_H */
