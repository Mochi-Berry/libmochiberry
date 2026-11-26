#!/bin/bash
# build_mbjs_split.sh — build the engine from the SPLIT modules.
#
# The former monolithic mbjs.c is now split into 14 per-feature modules
# (mbjs-core.c ... mbjs-builtin-misc.c) plus a shared mbjs-internal.h, so
# `mbjs` is replaced by that module list. Every other translation unit
# (dtoa, libregexp, libunicode, mbjs-libc, mbjs-mem, mbts, mbjs-superset,
# mbjs-inline, mbjs-jit, mbjs-llvm, mbjs-mulch, mbjs-opt, mbjs-routine,
# mbvm, mbjs-cli, mbjs-repl, standalone) is unchanged.
#
#   usage: ./build_mbjs_split.sh [outdir]     (default: build_splitfull)
#
# NOTE: the supporting TUs' .c files (mbjs-libc.c, mbjs-cli.c, ...) live in
# the engine's base source tree, NOT in this repo. Copy them in (or point
# at the tree that has them) before running, or pass their directory via
# SRC=... so `$SRC/mbjs-libc.c` is used. The split modules themselves are
# in this repo's root.
set -euo pipefail
cd "$(dirname "$0")"

OUT=${1:-build_splitfull}
SRC=${SRC:-.}
WLDIR=/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin
export PATH="$WLDIR:$PATH"
export TMPDIR=E:/tmp/mochiberry_tmp TEMP=E:/tmp/mochiberry_tmp TMP=E:/tmp/mochiberry_tmp
mkdir -p /e/tmp/mochiberry_tmp "$OUT"

CFLAGS="-O2 -g -std=gnu11 -D_GNU_SOURCE -I."

# The 14 split modules replace the monolith mbjs.c.
SPLIT="mbjs-core mbjs-bignum mbjs-runtime mbjs-parser mbjs-bytecode \
       mbjs-builtin-obj mbjs-builtin-num mbjs-builtin-re mbjs-builtin-refl \
       mbjs-builtin-map mbjs-builtin-prom mbjs-builtin-date mbjs-builtin-ta \
       mbjs-builtin-misc"
OTHER="dtoa libregexp libunicode mbjs-libc mbjs-mem mbts mbjs-superset \
       mbjs-inline mbjs-jit mbjs-llvm mbjs-mulch mbjs-opt mbjs-routine mbvm"

for f in $SPLIT; do
  "$WLDIR/gcc.exe" $CFLAGS -c "$f.c" -o "$OUT/$f.o" &
done
for f in $OTHER; do
  "$WLDIR/gcc.exe" $CFLAGS -c "$SRC/$f.c" -o "$OUT/$f.o" &
done
for f in mbjs-cli mbjs-repl; do
  "$WLDIR/gcc.exe" $CFLAGS -c "$SRC/$f.c" -o "$OUT/$f.o" &
done
"$WLDIR/gcc.exe" $CFLAGS -c gen/standalone.c -o "$OUT/standalone.o" &
wait

"$WLDIR/ar.exe" rcs "$OUT/libmbjs.a" $(for f in $SPLIT $OTHER; do echo "$OUT/$f.o"; done)
# NOTE: --export-all-symbols is REQUIRED: native plugin DLLs (mochiwasmy.dll,
# mochifs.dll, ...) resolve MBJS_* engine symbols from the host exe at runtime.
"$WLDIR/gcc.exe" -Wl,--export-all-symbols -o "$OUT/mbjs.exe" "$OUT/mbjs-cli.o" "$OUT/mbjs-repl.o" \
    "$OUT/standalone.o" "$OUT/libmbjs.a" -lm -lpthread
"$WLDIR/gcc.exe" $CFLAGS -o "$OUT/run-test262.exe" run-test262.c \
    "$OUT/libmbjs.a" -lm -lpthread

echo "[OK] $OUT/mbjs.exe        $(stat -c%s "$OUT/mbjs.exe") byte"
echo "[OK] $OUT/run-test262.exe $(stat -c%s "$OUT/run-test262.exe") byte"
