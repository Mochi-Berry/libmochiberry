#!/usr/bin/env bash
# ab_opt.sh — A/B isolate the optimization level of the engine TU.
#
#   build/ab-o2/mbjs.strip.exe   engine compiled with -O2  (current tier-full)
#   build/ab-o3/mbjs.strip.exe   engine compiled with -O3
#
# Everything else (other objects, link order, strip, flags) is identical to
# scripts/build-profiles.sh tier "full". MochiBerry builds with -O2 while
# quickjs-ng's CMake Release uses -O3 -DNDEBUG, so this checks whether the
# 1.27x gap vs quickjs-ng is just the optimization level.

set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT_NATIVE="$(cd "$ROOT" && pwd -W 2>/dev/null || echo "$ROOT")"
MINGW="/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
export PATH="$MINGW:$PATH"
CC="$MINGW/gcc.exe"; AR="$MINGW/ar.exe"; STRIP="$MINGW/strip.exe"
cd "$ROOT"

BASE_CFLAGS="-std=gnu11 -D_GNU_SOURCE -I. -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-ident -fno-jump-tables"
LDFLAGS="-Wl,--gc-sections"
ENGINE="dtoa libregexp libunicode mbjs mbjs-mem mbjs-mulch mbjs-superset mbjs-inline mbjs-jit mbjs-llvm"

build_variant() {   # $1 = name, $2 = opt level
    local name="$1" opt="$2"
    local out="build/ab-$name" out_win="$ROOT_NATIVE/build/ab-$name"
    mkdir -p "$out"
    echo "--- variant $name ($opt)"
    $CC $opt $BASE_CFLAGS -c mbjs.c -o "$out_win/mbjs.o" 2>"$out/mbjs.err" || {
        echo "   FAIL compile"; tail -5 "$out/mbjs.err"; return 1; }
    rm -f "$out/libmbjs.a"
    local objs=""
    for f in $ENGINE; do
        [ "$f" = "mbjs" ] && objs="$objs $out_win/mbjs.o" || objs="$objs $ROOT_NATIVE/build/tier-full/$f.o"
    done
    $AR rcs "$out_win/libmbjs.a" $objs || { echo "   FAIL ar"; return 1; }
    $CC $opt $LDFLAGS -o "$out_win/mbjs.exe" \
        "$ROOT_NATIVE/build/tier-full/mbjs-cli.o" \
        "$ROOT_NATIVE/build/tier-full/mbjs-repl.o" \
        "$ROOT_NATIVE/build/tier-full/standalone.o" \
        "$ROOT_NATIVE/build/tier-full/mbts.o" \
        "$ROOT_NATIVE/build/tier-full/mbjs-libc.o" \
        "$out_win/libmbjs.a" -lm -lpthread 2>"$out/link.err" || {
        echo "   FAIL link"; tail -10 "$out/link.err"; return 1; }
    cp "$out_win/mbjs.exe" "$out_win/mbjs.strip.exe"
    $STRIP -s "$out_win/mbjs.strip.exe" 2>/dev/null || true
    printf "   built %s  (%s bytes)\n" "$out_win/mbjs.strip.exe" "$(wc -c < "$out_win/mbjs.strip.exe")"
}

build_variant o2 "-O2"
build_variant o3 "-O3"

echo
echo "=== A/B: micro.js, interleaved 3x ==="
for i in 1 2 3; do
    echo "--- rep $i ---"
    printf "  o2 : "; "$ROOT_NATIVE/build/ab-o2/mbjs.strip.exe" 'E:/tmp/mbbench/micro.js' 2>&1 | tr "\n" " | "; echo
    printf "  o3 : "; "$ROOT_NATIVE/build/ab-o3/mbjs.strip.exe" 'E:/tmp/mbbench/micro.js' 2>&1 | tr "\n" " | "; echo
done

echo
echo "=== A/B: end-to-end workload, interleaved 4x ==="
for i in 1 2 3 4; do
    for v in o2 o3; do
        s=$(date +%s%N)
        "$ROOT_NATIVE/build/ab-$v/mbjs.strip.exe" "$ROOT_NATIVE/benchmarks/bench_workload.js" >/dev/null 2>&1
        e=$(date +%s%N)
        printf "  %-3s %6s ms\n" "$v" "$(( (e-s)/1000000 ))"
    done
done
