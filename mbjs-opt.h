/*
 * mbjs-opt.h — MochiBerry optimization ladder O0..O10.
 *
 * A single source of truth for the optimization levels shared by MBJSC,
 * mbjs-jit and mbvm --jit.  The ladder mirrors the Berry Lab "O0..O10"
 * design (and LunaBerry's luna-opt.h):
 *
 *   O0  Raw          no optimization, debug info kept
 *   O1  Clean        mem2reg / sroa / instcombine territory
 *   O2  Solid        standard -O2 (production default)
 *   O3  Fast         standard -O3 (vectorization, aggressive inlining)
 *   O4  Mochi        + loop unrolling
 *   O5  Mulch        + frame-pointer omission
 *   O6  Akari        + errno-free math
 *   O7  Trace        + hidden visibility
 *   O8  Silicon      + native tuning
 *   O9  Nirvana      + aggressive floating point
 *   O10 The Stripper + no debug info, no unwind tables, implicit strip,
 *                     "dead assembly" (no readable source identity)
 *
 * The compiler flags are plain clang/gcc flags, so every level is a real,
 * measurable change in the generated code -- not just a label.
 */

#ifndef MBJS_OPT_H
#define MBJS_OPT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MBJS_OPT_MIN 0
#define MBJS_OPT_MAX 10
#define MBJS_OPT_DEFAULT 10 /* O10 = optimized, stripped, dead asm */

/* Clamp any integer into the valid 0..10 range. */
int mbjs_opt_clamp(int level);

/* Human readable level name ("Raw", "Solid", "The Stripper", ...). */
const char *mbjs_opt_name(int level);

/* Compiler flags for a level (clang/gcc compatible). Never NULL. */
const char *mbjs_opt_flags(int level);

/* 1 when the level implies stripping the produced binary (O10). */
int mbjs_opt_implies_strip(int level);

/* 1 when the level implies "dead assembly" output (O10). */
int mbjs_opt_implies_dead_asm(int level);

/* 1 when the level implies that no debug information is emitted (O0/O10). */
int mbjs_opt_implies_no_debug(int level);

/* Emit warnings for flags that are redundant at this level.
 * `strip_given` / `debug_given` report whether the user also passed
 * --strip / -g style flags. */
void mbjs_opt_warn_redundant(int level, int strip_given, int debug_given);

/* Rewrite an assembly file into "dead assembly": comments, source identity
 * (.file), .ident, .comment, debug directives are removed and every
 * non-exported label is renamed to an anonymous .L<n>.  Exported symbols
 * (.globl / .def) keep their names because the ABI needs them.
 * Returns 0 on success. */
int mbjs_opt_dead_asm(const char *in_path, const char *out_path);

/* Remove the unwind directives (.seh_* / .cfi_*) from a textual assembly
 * file so the host GNU assembler can consume it.  Returns 0 on success. */
int mbjs_opt_strip_unwind(const char *path);

/* Strip a produced binary as hard as the host tools allow
 * (strip --strip-all, falling back to objcopy --strip-all).
 * Returns 0 on success, -1 if no strip tool was found. */
int mbjs_opt_strip_binary(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* MBJS_OPT_H */
