#!/usr/bin/env bash
# build mbjsc (the bytecode compiler) from the merged tree, then regenerate
# the generated sources (gen/*.c + builtin-*.h) with it.
set -u
MINGW="/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
export PATH="$MINGW:$PATH"
cd /e/bcf/mochiberry

OUT=build/codegen
mkdir -p "$OUT"
CFLAGS="-O2 -std=gnu11 -D_GNU_SOURCE -I. -ffunction-sections -fdata-sections"

echo "== compile objects =="
pids=""
for f in mbjsc mbjsc-namelist mbjsc-emit mbjsc-pseudo mbjsc-module mbjsc-compile mbjs mbjs-mem mbjs-mulch mbjs-superset mbjs-inline mbjs-llvm mbjs-opt mbjs-libc libregexp libunicode dtoa; do
  gcc.exe $CFLAGS -c "$f.c" -o "$OUT/$f.o" 2> "$OUT/$f.err" &
  pids="$pids $!"
done
fail=0
for p in $pids; do wait "$p" || fail=1; done
if [ "$fail" -ne 0 ]; then
  echo "COMPILE FAILED:"
  for e in "$OUT"/*.err; do grep -m3 "error:" "$e"; done
  exit 1
fi
echo "   all objects ok"

echo "== link mbjsc.exe =="
gcc.exe -O2 -o "$OUT/mbjsc.exe" "$OUT"/mbjsc.o "$OUT"/mbjsc-namelist.o "$OUT"/mbjsc-emit.o \
  "$OUT"/mbjsc-pseudo.o "$OUT"/mbjsc-module.o "$OUT"/mbjsc-compile.o \
  "$OUT"/mbjs-opt.o "$OUT"/mbjs.o "$OUT"/mbjs-mem.o \
  "$OUT"/mbjs-mulch.o "$OUT"/mbjs-superset.o "$OUT"/mbjs-inline.o "$OUT"/mbjs-llvm.o \
  "$OUT"/mbjs-libc.o "$OUT"/libregexp.o "$OUT"/libunicode.o "$OUT"/dtoa.o -lm -lpthread 2> "$OUT/link.err"
if [ -s "$OUT/link.err" ]; then echo "--- link.err ---"; head -20 "$OUT/link.err"; fi
if [ ! -f "$OUT/mbjsc.exe" ]; then echo "LINK FAILED"; exit 1; fi
echo "   mbjsc.exe ok ($(stat -c%s "$OUT/mbjsc.exe") bytes)"

# NOTE: mbjsc's own `-o` output is pseudo-C (human-readable, NOT compilable).
# The bytecode-array format that gen/*.c must contain is produced by the
# upstream qjsc generator (patched for the mbjs: module names) -- drive that
# pipeline instead of letting mbjsc write pseudo-C over the generated files.
echo "== codegen (upstream qjsc pipeline) =="
if ! python3 scripts/codegen-upstream.py; then
  echo "CODEGEN FAILED"; exit 1
fi
echo "== sanity: generated files must be bytecode arrays, not pseudo-C =="
ok=1
for f in gen/repl.c gen/standalone.c gen/hello.c builtin-iterator-zip.h; do
  if ! grep -q "const uint8_t" "$f"; then
    echo "  BUKAN bytecode array: $f (pseudo-C tertulis?)"; ok=0
  fi
done
[ "$ok" -eq 1 ] && echo "   ok: semua file generated berformat bytecode array"
echo "CODEGEN DONE"
