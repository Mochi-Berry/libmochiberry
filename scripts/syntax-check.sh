#!/usr/bin/env bash
# syntax/compile check every translation unit of the MochiBerry tree
set -u
MINGW="/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
export PATH="$MINGW:$PATH"
cd /e/bcf/mochiberry

OUT=build/syntax
mkdir -p "$OUT"
CFLAGS="-O1 -std=gnu11 -D_GNU_SOURCE -I. -fsyntax-only"

FILES="mbjs mbjs-libc mbjs-mem mbjs-mulch mbjs-superset mbjs-inline mbjs-jit mbjs-llvm
       mbjs-cli mbjs-repl mbjs-opt mbjs-routine mbts mbvm
       mbjsc mbjsc-namelist mbjsc-emit mbjsc-pseudo mbjsc-module mbjsc-compile
       run-test262 api-test ctest"

fail=0
for f in $FILES; do
  [ -f "$f.c" ] || { echo "SKIP  $f.c (tidak ada)"; continue; }
  if gcc.exe $CFLAGS "$f.c" > "$OUT/$f.out" 2>&1; then
    echo "OK    $f.c"
  else
    echo "FAIL  $f.c"
    head -8 "$OUT/$f.out"
    fail=1
  fi
done

echo "== marker konflik yang tertinggal =="
if grep -rlE '^(<<<<<<<|>>>>>>>|=======)$' --include='*.c' --include='*.h' --include='*.js' . 2>/dev/null | head -20 | grep -q .; then
  grep -rlE '^(<<<<<<<|>>>>>>>|=======)$' --include='*.c' --include='*.h' --include='*.js' . 2>/dev/null | head -20
  fail=1
else
  echo "   bersih (tidak ada marker konflik)"
fi

echo "EXIT=$fail"
exit $fail
