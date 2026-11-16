#!/bin/bash
# build_manual2: build mbjs + run-test262 dari sumber TERKINI (mbjs.c Nov 13)
# ke direktori terpisah supaya binary lama (Nov 8) tetap ada untuk perbandingan.
#
#   usage: ./build_mbjs.sh [outdir]
set -euo pipefail
cd "$(dirname "$0")"

OUT=${1:-build_manual2}
GCC=/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin/gcc.exe
# The gcc above lives in the WinLibs mingw64/bin tree -- that is also where
# `ar` lives, and `ar` is NOT on PATH on this host (the user PATH still points
# at a scoop\apps\gcc tree that no longer exists).  Put that tree on PATH so
# the archive step finds ar.
WLDIR=/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin
export PATH="$WLDIR:$PATH"
export TMPDIR=E:/tmp/mochiberry_tmp TEMP=E:/tmp/mochiberry_tmp TMP=E:/tmp/mochiberry_tmp
mkdir -p /e/tmp/mochiberry_tmp "$OUT"

CFLAGS="-O2 -g -std=gnu11 -D_GNU_SOURCE -I."
# The archive must contain every translation unit the engine links against:
# mbjs-superset.c (mbjs_superset_lookup) and mbjs-inline.c (mbjs_inline_init)
# are referenced by mbjs.c/mbjs-libc.c and were missing here -> the link
# failed with "undefined reference".  Keep this list in sync with the .o set
# of the last known-good build/ directory.
LIBSRC="dtoa libregexp libunicode mbjs mbjs-libc mbjs-mem mbts mbjs-superset mbjs-inline mbjs-jit mbjs-llvm mbjs-mulch mbjs-opt mbjs-routine mbvm"

echo "[i] gcc: $("$GCC" --version | head -1)"
echo "[i] sumber: $(stat -c '%y %n' mbjs.c)"
mkdir -p "$OUT"

# kompilasi paralel: file besar (mbjs.c) belakangan tapi semua paralel
for f in $LIBSRC; do
  "$GCC" $CFLAGS -c "$f.c" -o "$OUT/$f.o" &
done
for f in mbjs-cli mbjs-repl; do
  "$GCC" $CFLAGS -c "$f.c" -o "$OUT/$f.o" &
done
"$GCC" $CFLAGS -c gen/standalone.c -o "$OUT/standalone.o" &
wait

ar rcs "$OUT/libmbjs.a" $(for f in $LIBSRC; do echo "$OUT/$f.o"; done)
"$GCC" -o "$OUT/mbjs.exe" "$OUT/mbjs-cli.o" "$OUT/mbjs-repl.o" "$OUT/standalone.o" "$OUT/libmbjs.a" -lm -lpthread
"$GCC" $CFLAGS -o "$OUT/run-test262.exe" run-test262.c "$OUT/libmbjs.a" -lm -lpthread

echo "[OK] $OUT/mbjs.exe        $(stat -c%s "$OUT/mbjs.exe") byte"
echo "[OK] $OUT/run-test262.exe $(stat -c%s "$OUT/run-test262.exe") byte"
