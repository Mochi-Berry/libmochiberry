/*
 * mbts.h - MochiBerry TypeScript superset front-end (--superset=typescript)
 *
 * Purpose: let the MochiBerry *runtime* (no npm, no tsc, no node_modules, no
 * wireweight toolchain) execute .ts / .tsx / .mts / .cts sources directly.
 *
 * Design (Bun/Deno-like, but STRICTLY lighter):
 *   - This is as a *type eraser*: types are erased, not checked. Erased output is
 *     valid JS (quickjs-ng compiles it). No type checker is shipped - that is
 *     the one thing that cannot be "light", so it stays out.
 *   - Type-only constructs that do not lower 1:1 to JS are either
 *       (a) lowered here with a *trivial* transform (exc, only forms whose
 *          semantics are unambiguous and cost O(1) to emit), or
 *       (b) refused with a precise error (line:col) telling the user to
 *          pre-compile - never silently mangled.
 *   - Erased output is *byte-offset preserving*: erased type text is replaced by
 *     a single ' ' where a separator is needed and by nothing where it is not,
 *     except that we pad with spaces to keep the original length so that line
 *     numbers/columns in runtime errors still point at the .ts source. This is
 *     the same trick as Bun's native transpiler ("source-preserving output").
 *
 * Supported ("erasable + light transform"):
 *   variable / parameter / return / property / method type annotations
 *   interface, `type X = ...` (with lazy bodies), `declare` statements
 *   `enum` and `const enum`   -> real JS objects (+ reverse map for numbers)
 *   `namespace` / `module`    -> IIFE object
 *   class field `x!: T` / `x: T` -> `x`  (with `useDefineForClassFields`)
 *   `private`/`public`/`protected`/`readonly`/`override`/`abstract` modifiers
 *   parameter properties       (constructor(private x: number))
 *   generic parameter lists    <T>, <T extends U = D>
 *   `import type` / `export type` / `import { type X }`
 *   type assertions `x as T` -> erased;  `x!` non-null -> erased
 *   `satisfies T`              -> erased
 *   `as const`                 -> erased
 *   `declare` members/stmts    -> erased
 *   decorators (@dec)          -> transformed into `__decorate` calls
 *   `import x = require("y")`  -> `import x from "y"`  (esm assumption)
 *   `export =` / `import =`    -> refused with guidance
 *
 * NOT supported (explicit errors, no silent breakage):
 *   const enums across modules (inlined here only in same file)
 *   `export =` CJS interop (use `export default`)
 *   .tsx JSX syntax (needs a JSX runtime decision -> refuse, point at the flag)
 *   decorators on parameters (needs metadata reflection)
 */
#ifndef MBTS_H
#define MBTS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* set by the CLI when --superset=typescript is given */
extern bool mbts_superset;

typedef enum {
    MBTS_OK = 0,          /* output is valid JS (types erased)            */
    MBTS_DIAG,           /* refused: message points at source position   */
    MBTS_OOM,            /* allocation failure                           */
} MBTSStatus;

/*
 * Erase TypeScript syntax from `src` (UTF-8, len bytes).
 *
 * On MBTS_OK      -> *out / *out_len hold the JS source (caller frees via MBTS_Free).
 * On MBTS_DIAG    -> *out is NULL, *diag_line / *diag_col are 1-based and *diag_msg
 *                    is a static string, e.g. "`enum` with initializers needs ...".
 * On MBTS_OOM     -> diag_msg is "out of memory".
 */
MBTSStatus mbts_strip(const char *src, size_t len, int is_tsx,
                      const char **diag_msg, int *diag_line, int *diag_col,
                      char **out, size_t *out_len);

void MBTS_Free(char *p);

/* heuristic: does this filename imply TypeScript? */
bool mbts_is_ts_filename(const char *filename);

#endif /* MBTS_H */
