#!/usr/bin/env bash
# variant build: swap in a variant mbjs.c, rebuild mbjs.o, relink CLI, run a test
set -u
VARIANT="$1"     # path to variant mbjs.c
TESTJS="$2"      # test to run
MINGW="/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UPRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
MINGW="/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
export PATH="$MINGW:$PATH"
cd /e/bcf/mochiberry

OUT=build/variant
mkdir -p "$OUT"
CFLAGS="-O2 -std=gnu11 -D_GNU_SOURCE -I. -ffunction-sections -fdata-sections"

cp -f mbjs.c "$OUT/mbjs.c.orig"
cp -f "$VARIANT" mbjs.c

# reuse the other objects if present, else build them
for f in dtoa libregexp libunicode mbjs-mem mbjs-mulch mbjs-superset mbjs-inline mbjs-libc mbjs-cli mbjs-repl mbts; do
  if [ ! -f "$OUT/$f.o" ]; then
    gcc.exe $CFLAGS -c "$f.c" -o "$OUT/$f.o" 2> "$OUT/$f.err" || { echo "COMPILE FAIL $f"; grep -m3 "error:" "$OUT/$f.err"; cp -f "$OUT/mbjs.c.orig" mbjs.c; exit 1; }
  fi
done

gcc.exe $CFLAGS -c mbjs.c -o "$OUT/mbjs.o" 2> "$OUT/mbjs.err" || { echo "COMPILE FAIL mbjs"; grep -m5 "error:" "$OUT/mbjs.err"; cp -f "$OUT/mbjs.c.orig" mbjs.c; exit 1; }

ar rcs "$OUT/libmbjs.a" "$OUT"/dtoa.o "$OUT"/libregexp.o "$OUT"/libunicode.o "$OUT"/mbjs.o "$OUT"/mbjs-mem.o "$OUT"/mbjs-mulch.o "$OUT"/mbjs-superset.o "$OUT"/mbjs-inline.o
gcc.exe -O2 -Wl,--gc-sections -o "$OUT/mbjs.exe" "$OUT"/mbjs-cli.o "$OUT"/mbjs-repl.o "$OUT"/standalone.o "$OUT"/mbts.o "$OUT"/mbjs-libc.o "$OUT"/libmbjs.a -lm -lpthread 2> "$OUT/link.err"
if [ ! -f "$OUT/mbjs.exe" ]; then echo "LINK FAIL"; head -8 "$OUT/link.err"; cp -f "$OUT/mbjs.c.orig" mbjs.c; exit 1; fi

echo "== variant: $(basename "$VARIANT") =="
timeout 60 "$OUT/mbjs.exe" "$TESTJS" 2>&1 | head -5
echo "exit=${PIPESTATUS[0]}"

cp -f "$OUT/mbjs.c.orig" mbjs.c
echo "mbjs.c dipulihkan"
