/*
 * mbjs-profile.h — compile-time size/speed profiles for MochiBerry.
 *
 * The profiles are *build-time* switches, not runtime checks: the code for
 * a disabled feature is never compiled into the binary, so the linker can
 * drop it entirely (see scripts/build-profiles.sh).
 *
 *   MBJS_PROFILE_SLIM  smallest language core.
 *                      Keeps: Object, Function, Array, String, Number,
 *                      Boolean, Symbol, Error, Math, Reflect, the parser,
 *                      the bytecode VM, Promise (needed by module eval),
 *                      eval, and the JSON subset that the REPL needs.
 *                      Drops: Date, RegExp, Proxy, Map/Set, TypedArrays,
 *                      BigInt, WeakRef, AToB, Performance, Atomics.
 *
 *   MBJS_PROFILE_CORE  default "medium" build. Everything in SLIM plus
 *                      Date, RegExp, JSON, Map/Set, Proxy and BigInt.
 *                      Drops only the heavyweight/niche parts:
 *                      TypedArrays, Atomics, WeakRef, AToB, Performance.
 *
 *   MBJS_PROFILE_FULL  everything (default when no profile is selected).
 *
 * Every profile can be combined with the GC/allocator switches that already
 * exist (MBJS_SUNAT_GC) and with the optimization ladder (mbjs-opt.h).
 *
 * Feature macros are also usable directly, e.g. -DMBJS_NO_REGEXP=1, which
 * is how the profile headers expand.  The engine source tests them with
 * #ifdef, so a feature is compiled out entirely when its macro is defined.
 */

#ifndef MBJS_PROFILE_H
#define MBJS_PROFILE_H

/* --- individual feature switches (all default OFF) --------------------- */
#ifndef MBJS_NO_DATE
/* #define MBJS_NO_DATE */
#endif
#ifndef MBJS_NO_REGEXP
/* #define MBJS_NO_REGEXP */
#endif
#ifndef MBJS_NO_JSON
/* #define MBJS_NO_JSON */
#endif
#ifndef MBJS_NO_PROXY
/* #define MBJS_NO_PROXY */
#endif
#ifndef MBJS_NO_MAPSET
/* #define MBJS_NO_MAPSET */
#endif
#ifndef MBJS_NO_TYPEDARRAYS
/* #define MBJS_NO_TYPEDARRAYS */
#endif
#ifndef MBJS_NO_BIGINT
/* #define MBJS_NO_BIGINT */
#endif
#ifndef MBJS_NO_ATOMICS
/* #define MBJS_NO_ATOMICS */
#endif
#ifndef MBJS_NO_WEAKREF
/* #define MBJS_NO_WEAKREF */
#endif
#ifndef MBJS_NO_ATOB
/* #define MBJS_NO_ATOB */
#endif
#ifndef MBJS_NO_PERFORMANCE
/* #define MBJS_NO_PERFORMANCE */
#endif
#ifndef MBJS_NO_DOM_EXCEPTION
/* #define MBJS_NO_DOM_EXCEPTION */
#endif

/* --- profile expansion -------------------------------------------------- */
#if defined(MBJS_PROFILE_SLIM)
#define MBJS_NO_DATE 1
#define MBJS_NO_REGEXP 1
#define MBJS_NO_JSON 1
#define MBJS_NO_PROXY 1
#define MBJS_NO_MAPSET 1
#define MBJS_NO_TYPEDARRAYS 1
#define MBJS_NO_BIGINT 1
#define MBJS_NO_ATOMICS 1
#define MBJS_NO_WEAKREF 1
#define MBJS_NO_ATOB 1
#define MBJS_NO_PERFORMANCE 1
#define MBJS_NO_DOM_EXCEPTION 1
#define MBJS_PROFILE_NAME "slim"
#elif defined(MBJS_PROFILE_CORE)
#define MBJS_NO_TYPEDARRAYS 1
#define MBJS_NO_ATOMICS 1
#define MBJS_NO_WEAKREF 1
#define MBJS_NO_ATOB 1
#define MBJS_NO_PERFORMANCE 1
#define MBJS_NO_DOM_EXCEPTION 1
#define MBJS_PROFILE_NAME "core"
#else
#define MBJS_PROFILE_NAME "full"
#endif

/* Legacy spelling: MBJS_SLIM used to mean "drop the optional intrinsics at
 * runtime".  Keep it working, but map it to the real slim profile so the
 * binary actually shrinks. */
#if defined(MBJS_SLIM) && !defined(MBJS_PROFILE_SLIM)
#define MBJS_PROFILE_SLIM 1
#endif

#endif /* MBJS_PROFILE_H */
