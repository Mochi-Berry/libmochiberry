#!/usr/bin/env bash
# ab_jump_tables.sh — A/B isolate ONE build variable: -fno-jump-tables.
#
# Everything else (flags, sources, link order, strip) is byte-identical to
# scripts/build-profiles.sh tier "full". Two CLIs are produced:
#   build/ab-jt/mbjs.strip.exe    -> engine compiled WITH jump tables
#   build/ab-nojt/mbjs.strip.exe  -> engine compiled with -fno-jump-tables
#
# The build-profiles.sh comment claims -fno-jump-tables has "speed unchanged
# (measured on a CPU-bound workload)". This script checks that claim.

set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT_NATIVE="$(cd "$ROOT" && pwd -W 2>/dev/null || echo "$ROOT")"
MINGW="/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
export PATH="$MINGW:$PATH"
CC="$MINGW/gcc.exe"; AR="$MINGW/ar.exe"; STRIP="$MINGW/strip.exe"
cd "$ROOT"

BASE_CFLAGS="-O2 -std=gnu11 -D_GNU_SOURCE -I. -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-ident"
LDFLAGS="-Wl,--gc-sections"
ENGINE="dtoa libregexp libunicode mbjs mbjs-mem mbjs-mulch mbjs-superset mbjs-inline mbjs-jit mbjs-llvm"

build_variant() {   # $1 = name, $2 = extra flag ("" or "-fno-jump-tables")
    local name="$1" extra="$2"
    local out="build/ab-$name" out_win="$ROOT_NATIVE/build/ab-$name"
    mkdir -p "$out"
    echo "--- variant $name (extra='$extra')"
    $CC $BASE_CFLAGS $extra -c mbjs.c -o "$out_win/mbjs.o" 2>"$out/mbjs.err" || {
        echo "   FAIL compile"; tail -5 "$out/mbjs.err"; return 1; }
    # libmbjs.a: same members, mbjs.o swapped
    rm -f "$out/libmbjs.a"
    local objs=""
    for f in $ENGINE; do
        [ "$f" = "mbjs" ] && objs="$objs $out_win/mbjs.o" || objs="$objs $ROOT_NATIVE/build/tier-full/$f.o"
    done
    $AR rcs "$out_win/libmbjs.a" $objs || { echo "   FAIL ar"; return 1; }
    $CC -O2 $LDFLAGS -o "$out_win/mbjs.exe" \
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

build_variant jt ""
build_variant nojt "-fno-jump-tables"

echo
echo "=== A/B: micro.js, interleaved 3x ==="
for i in 1 2 3; do
    echo "--- rep $i ---"
    printf "  jt   : "; "$ROOT_NATIVE/build/ab-jt/mbjs.strip.exe"   'E:/tmp/mbbench/micro.js' 2>&1 | tr "\n" " | "; echo
    printf "  nojt : "; "$ROOT_NATIVE/build/ab-nojt/mbjs.strip.exe" 'E:/tmp/mbbench/micro.js' 2>&1 | tr "\n" " | "; echo
done

echo
echo "=== A/B: end-to-end workload, interleaved 3x ==="
for i in 1 2 3; do
    for v in jt nojt; do
        s=$(date +%s%N)
        "$ROOT_NATIVE/build/ab-$v/mbjs.strip.exe" "$ROOT_NATIVE/benchmarks/bench_workload.js" >/dev/null 2>&1
        e=$(date +%s%N)
        printf "  %-5s %6s ms\n" "$v" "$(( (e-s)/1000000 ))"
    done
done
