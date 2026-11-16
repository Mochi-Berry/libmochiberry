#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# ab_flags.sh -- is the 1.4x gap caused by the BUILD FLAGS?
#
# The two engines are near-identical in source (mbjs.c 65398 lines vs
# quickjs.c 65028; the opcode table is byte-identical; the interpreter loop
# differs by 14 lines out of 4418; C-call counts and executed-opcode counts
# are identical). The remaining difference is the compiler command line:
#
#   MochiBerry : -O2 -std=gnu11 -D_GNU_SOURCE -ffunction-sections
#                -fdata-sections -fno-asynchronous-unwind-tables -fno-ident
#                -fno-jump-tables   +  link -Wl,--gc-sections
#   QuickJS-ng : -O2   (nothing else)
#
# This script builds BOTH directions and measures them interleaved:
#   A  QuickJS-ng, plain -O2                    (reference)
#   B  QuickJS-ng, MochiBerry's flags
#   C  MochiBerry, MochiBerry's flags           (current)
#   D  MochiBerry, plain -O2
#
# Usage: bash scripts/ab_flags.sh [reps]
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="/e/bcf/mochiberry"
ROOT_WIN="$(cd "$ROOT" && pwd -W)"
QJS_DIR="/e/tmp/qjsng/quickjs-0.18.0"
QJS_DIR_WIN="$(cd "$QJS_DIR" && pwd -W)"
MINGW="${MINGW:-/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin}"
export PATH="$MINGW:$PATH"
CC="$MINGW/gcc.exe"
REPS="${1:-3}"

OUT="$ROOT/build/abflags"
OUT_WIN="$ROOT_WIN/build/abflags"
mkdir -p "$OUT"

MB_FLAGS="-O2 -std=gnu11 -D_GNU_SOURCE -I$ROOT_WIN -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-ident -fno-jump-tables"
MB_PLAIN="-O2 -std=gnu11 -D_GNU_SOURCE -I$ROOT_WIN"

ENGINE="dtoa libregexp libunicode mbjs mbjs-mem mbjs-mulch mbjs-superset mbjs-inline mbjs-jit mbjs-llvm"
CLI="mbjs-cli mbjs-repl"
EXTRA="mbts mbjs-libc"

build_mochi() {
    # $1 = variant name, $2 = cflags, $3 = extra ldflags
    local name="$1" cflags="$2" ldflags="$3"
    local o="$OUT/$name"
    local ow="$OUT_WIN/$name"
    mkdir -p "$ow"
    local objs=""
    for f in $ENGINE $CLI $EXTRA; do
        $CC $cflags -c "$ROOT_WIN/$f.c" -o "$ow/$f.o" 2>"$o/$f.err" || {
            echo "FAIL compile $f ($name)"; head -5 "$o/$f.err"; return 1; }
        objs="$objs $ow/$f.o"
    done
    $CC $cflags -c "$ROOT_WIN/gen/standalone.c" -o "$ow/standalone.o" 2>"$o/standalone.err" || return 1
    $CC $cflags $ldflags -o "$ow/mbjs.exe" $objs "$ow/standalone.o" -lm -lpthread 2>"$o/link.err" || {
        echo "FAIL link ($name)"; head -10 "$o/link.err"; return 1; }
    echo "   built $ow/mbjs.exe"
}

build_qjs() {
    # $1 = variant name, $2 = cflags
    local name="$1" cflags="$2"
    ( cd "$QJS_DIR" && $CC $cflags -w -o "$OUT_WIN/qjs_$name.exe" \
        qjs.c gen/repl.c gen/standalone.c quickjs.c quickjs-libc.c \
        libregexp.c libunicode.c dtoa.c -lm -lws2_32 ) 2>"$OUT/qjs_$name.err" || {
            echo "FAIL qjs build ($name)"; head -10 "$OUT/qjs_$name.err"; return 1; }
    echo "   built $OUT_WIN/qjs_$name.exe"
}

echo "==> A: QuickJS-ng plain -O2"
build_qjs plain "-O2"
echo "==> B: QuickJS-ng with MochiBerry's flags"
build_qjs mbflags "$MB_FLAGS -I$QJS_DIR_WIN"
echo "==> C: MochiBerry current flags"
build_mochi mb_current "$MB_FLAGS" "-Wl,--gc-sections"
echo "==> D: MochiBerry plain -O2"
build_mochi mb_plain "$MB_PLAIN" ""

PROBE="E:/tmp/mbbench/loopprobe.js"
PROBE2="E:/tmp/mbbench/callprobe.js"

declare -a NAMES=(qjs_plain qjs_mbflags mb_current mb_plain)
declare -a BINS=("$OUT_WIN/qjs_plain.exe" "$OUT_WIN/qjs_mbflags.exe" \
                "$OUT_WIN/mb_current/mbjs.exe" "$OUT_WIN/mb_plain/mbjs.exe")

echo
echo "==> measuring (interleaved, reps=$REPS)"
for probe in "$PROBE2" "$PROBE"; do
    pname=$(basename "$probe")
    echo "--- probe: $pname ---"
    declare -A best
    for i in $(seq 1 $REPS); do
        for idx in 0 1 2 3; do
            s=$(date +%s%N)
            "${BINS[$idx]}" "$probe" >/dev/null 2>&1
            e=$(date +%s%N)
            ms=$(( (e - s) / 1000000 ))
            n="${NAMES[$idx]}"
            cur=${best[$n]:-999999}
            [ "$ms" -lt "$cur" ] && best[$n]=$ms
        done
    done
    for idx in 0 1 2 3; do
        n="${NAMES[$idx]}"
        printf "   %-12s min=%s ms\n" "$n" "${best[$n]}"
    done
done
