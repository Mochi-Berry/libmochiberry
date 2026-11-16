#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# profile_tiers.sh -- gprof comparison of the JS call path.
#
# Builds an instrumented (-pg) MochiBerry and QuickJS-ng, runs the SAME
# call-heavy probe in both, and reports:
#   * flat profile  (where the time goes)
#   * call counts   (how many C-level calls each engine needs per JS call)
#
# The call counts are the interesting part: the two engines look identical at
# the source level, so a 1.4x gap is usually a per-call work multiplier
# (helper calls that the other engine inlines, extra lookups, ...).
#
# Usage: bash scripts/profile_tiers.sh [probe.js]
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="/e/bcf/mochiberry"
ROOT_WIN="$(cd "$ROOT" && pwd -W)"
QJS_DIR="/e/tmp/qjsng/quickjs-0.18.0"
QJS_DIR_WIN="$(cd "$QJS_DIR" && pwd -W)"
MINGW="${MINGW:-/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin}"
export PATH="$MINGW:$PATH"
CC="$MINGW/gcc.exe"
PROBE="${1:-E:/tmp/mbbench/callprobe.js}"
PROBE_WIN="$PROBE"

OUT="$ROOT/build/prof"
OUT_WIN="$ROOT_WIN/build/prof"
mkdir -p "$OUT"

ENGINE="dtoa libregexp libunicode mbjs mbjs-mem mbjs-mulch mbjs-superset mbjs-inline mbjs-jit mbjs-llvm"
CLI="mbjs-cli mbjs-repl"
EXTRA="mbts mbjs-libc"

echo "==> building instrumented MochiBerry (-O2 -pg -g)"
objs=""
for f in $ENGINE $CLI $EXTRA; do
    # reuse a previous instrumented object unless the source is newer
    if [ -f "$OUT_WIN/$f.o" ] && [ "$OUT_WIN/$f.o" -nt "$ROOT_WIN/$f.c" ]; then
        objs="$objs $OUT_WIN/$f.o"
        continue
    fi
    "$CC" -O2 -pg -g -std=gnu11 -D_GNU_SOURCE -I"$ROOT_WIN" \
        -c "$ROOT_WIN/$f.c" -o "$OUT_WIN/$f.o" 2>"$OUT/$f.err" || {
            echo "FAIL compile $f"; head -5 "$OUT/$f.err"; exit 1; }
    objs="$objs $OUT_WIN/$f.o"
done
"$CC" -O2 -pg -g -std=gnu11 -D_GNU_SOURCE -I"$ROOT_WIN" \
    -c "$ROOT_WIN/gen/standalone.c" -o "$OUT_WIN/standalone.o" 2>"$OUT/standalone.err" || {
        echo "FAIL standalone"; head -5 "$OUT/standalone.err"; exit 1; }
"$CC" -O2 -pg -g -o "$OUT_WIN/mbjs_prof.exe" $objs "$OUT_WIN/standalone.o" -lm -lpthread 2>"$OUT/link.err" || {
    echo "FAIL link"; head -10 "$OUT/link.err"; exit 1; }
echo "   built $OUT_WIN/mbjs_prof.exe"

echo "==> building instrumented QuickJS-ng 0.18.0 (-O2 -pg -g)"
( cd "$QJS_DIR" && "$CC" -O2 -pg -g -w \
    -o "$OUT_WIN/qjs_prof.exe" qjs.c gen/repl.c gen/standalone.c quickjs.c \
    quickjs-libc.c libregexp.c libunicode.c dtoa.c -lm -lws2_32 ) 2>"$OUT/qjs.err" || {
        echo "FAIL qjs build"; head -10 "$OUT/qjs.err"; exit 1; }
echo "   built $OUT_WIN/qjs_prof.exe"

run_one() {
    # $1 = label, $2 = exe (native), $3 = probe (native)
    local label="$1" exe="$2" probe="$3"
    ( cd "$OUT" && rm -f gmon.out && "$exe" "$probe" >/dev/null 2>&1 )
    if [ ! -f "$OUT/gmon.out" ]; then
        echo "!! $label: no gmon.out"
        return 1
    fi
    gprof "$exe" "$OUT_WIN/gmon.out" > "$OUT/$label.gprof" 2>/dev/null
    echo "-- $label flat profile (top 18) --"
    sed -n '/Flat profile/,/^$/p' "$OUT/$label.gprof" | head -20
    echo
}

run_one mochiberry "$OUT_WIN/mbjs_prof.exe" "$PROBE_WIN"
run_one quickjsng "$OUT_WIN/qjs_prof.exe" "$PROBE_WIN"

echo "==> call counts for the hot functions"
for fn in JS_CallInternal MBJS_CallInternal mbjs_dup mbjs_shape_find mbjs_find_own_property; do
    for label in mochiberry quickjsng; do
        n=$(awk -v f="$fn" '$1==f {print $2}' "$OUT/$label.gprof" 2>/dev/null | head -1)
        [ -n "$n" ] && printf "  %-14s %-14s %s\n" "$fn" "$label" "$n"
    done
done
echo
echo "gprof files: $OUT/mochiberry.gprof  $OUT/quickjsng.gprof"
