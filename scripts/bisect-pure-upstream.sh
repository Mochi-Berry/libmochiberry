#!/usr/bin/env bash
# bisect: build with a PURE upstream-0.18.0-branded mbjs.c and run tests/bug1661.js
set -u
MINGW="/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
export PATH="$MINGW:$PATH"
cd /e/bcf/mochiberry

cp -f mbjs.c mbjs.c.merged0180.bak
cp -f /e/tmp/opc/up018-branded/mbjs.c mbjs.c

OUT=build/bisect-pure
mkdir -p "$OUT"
CFLAGS="-O2 -std=gnu11 -D_GNU_SOURCE -I. -ffunction-sections -fdata-sections"
pids=""
for f in dtoa libregexp libunicode mbjs mbjs-mem mbjs-mulch mbjs-superset mbjs-inline mbjs-libc mbjs-cli mbjs-repl mbts; do
  gcc.exe $CFLAGS -c "$f.c" -o "$OUT/$f.o" 2> "$OUT/$f.err" &
  pids="$pids $!"
done
gcc.exe $CFLAGS -c gen/standalone.c -o "$OUT/standalone.o" 2> "$OUT/standalone.err" &
pids="$pids $!"
fail=0
for p in $pids; do wait "$p" || fail=1; done
if [ "$fail" -ne 0 ]; then
  echo "COMPILE FAILED"; for e in "$OUT"/*.err; do grep -m3 "error:" "$e"; done
  cp -f mbjs.c.merged0180.bak mbjs.c; exit 1
fi
ar rcs "$OUT/libmbjs.a" "$OUT"/dtoa.o "$OUT"/libregexp.o "$OUT"/libunicode.o "$OUT"/mbjs.o "$OUT"/mbjs-mem.o "$OUT"/mbjs-mulch.o "$OUT"/mbjs-superset.o "$OUT"/mbjs-inline.o
gcc.exe -O2 -Wl,--gc-sections -o "$OUT/mbjs.exe" "$OUT"/mbjs-cli.o "$OUT"/mbjs-repl.o "$OUT"/standalone.o "$OUT"/mbts.o "$OUT"/mbjs-libc.o "$OUT"/libmbjs.a -lm -lpthread 2> "$OUT/link.err"
if [ ! -f "$OUT/mbjs.exe" ]; then echo "LINK FAILED"; head -10 "$OUT/link.err"; cp -f mbjs.c.merged0180.bak mbjs.c; exit 1; fi

echo "== pure-upstream engine: jalankan bug1661.js =="
timeout 60 "$OUT/mbjs.exe" tests/bug1661.js 2>&1 | head -5
echo "exit=${PIPESTATUS[0]}"

# restore
cp -f mbjs.c.merged0180.bak mbjs.c
echo "mbjs.c dipulihkan"
