#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# callcount.sh -- exact C-level call counts for the JS call path.
#
# Why not gprof: MinGW's gprof on this host reads the -pg gmon.out as empty
# (no flat profile, no call graph), so the call counts are collected with
# GCC's -finstrument-functions hook instead (exact, no sampling).
#
# Method:
#   1. build MochiBerry and QuickJS-ng with -finstrument-functions
#   2. run an EMPTY script   -> baseline counts (startup/parse)
#   3. run the workload       -> workload counts
#   4. subtract baseline, map addresses to symbols via nm, sort by count
#
# Usage: bash scripts/callcount.sh [workload.js]
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="/e/bcf/mochiberry"
ROOT_WIN="$(cd "$ROOT" && pwd -W)"
QJS_DIR="/e/tmp/qjsng/quickjs-0.18.0"
QJS_DIR_WIN="$(cd "$QJS_DIR" && pwd -W)"
MINGW="${MINGW:-/c/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin}"
export PATH="$MINGW:$PATH"
CC="$MINGW/gcc.exe"
PROBE_WIN="${1:-E:/tmp/mbbench/callprobe.js}"
PROBE_BASE_WIN="${PROBE_WIN%.js}.base.js"

OUT="$ROOT/build/callcount"
OUT_WIN="$ROOT_WIN/build/callcount"
mkdir -p "$OUT"

# baseline script: same file name, empty body
printf 'var x = 0;\n' > "$PROBE_BASE_WIN"

ENGINE="dtoa libregexp libunicode mbjs mbjs-mem mbjs-mulch mbjs-superset mbjs-inline mbjs-jit mbjs-llvm"
CLI="mbjs-cli mbjs-repl"
EXTRA="mbts mbjs-libc"

echo "==> building instrumented MochiBerry (-finstrument-functions)"
objs=""
for f in $ENGINE $CLI $EXTRA; do
    if [ -f "$OUT_WIN/$f.o" ] && [ "$OUT_WIN/$f.o" -nt "$ROOT_WIN/$f.c" ]; then
        objs="$objs $OUT_WIN/$f.o"
        continue
    fi
    "$CC" -O2 -finstrument-functions -std=gnu11 -D_GNU_SOURCE -I"$ROOT_WIN" \
        -c "$ROOT_WIN/$f.c" -o "$OUT_WIN/$f.o" 2>"$OUT/$f.err" || {
            echo "FAIL compile $f"; head -5 "$OUT/$f.err"; exit 1; }
    objs="$objs $OUT_WIN/$f.o"
done
"$CC" -O2 -finstrument-functions -std=gnu11 -D_GNU_SOURCE -I"$ROOT_WIN" \
    -c "$ROOT_WIN/gen/standalone.c" -o "$OUT_WIN/standalone.o" 2>"$OUT/standalone.err" || {
        echo "FAIL standalone"; head -5 "$OUT/standalone.err"; exit 1; }
"$CC" -O2 -c "E:/tmp/profcount.c" -o "$OUT_WIN/profcount.o" || {
    echo "FAIL profcount"; exit 1; }
"$CC" -O2 -o "$OUT_WIN/mbjs_cc.exe" $objs "$OUT_WIN/standalone.o" "$OUT_WIN/profcount.o" \
    -lm -lpthread 2>"$OUT/link.err" || {
        echo "FAIL link"; head -10 "$OUT/link.err"; exit 1; }
echo "   built $OUT_WIN/mbjs_cc.exe"

echo "==> building instrumented QuickJS-ng 0.18.0"
# profcount.c MUST NOT be compiled with -finstrument-functions: the hook would
# instrument itself and recurse until the stack dies.
( cd "$QJS_DIR" && "$CC" -O2 -finstrument-functions -w \
    -o "$OUT_WIN/qjs_cc.exe" qjs.c gen/repl.c gen/standalone.c quickjs.c \
    quickjs-libc.c libregexp.c libunicode.c dtoa.c -lm -lws2_32 \
    "$OUT_WIN/profcount.o" ) 2>"$OUT/qjs.err" || {
        echo "FAIL qjs build"; head -10 "$OUT/qjs.err"; exit 1; }
echo "   built $OUT_WIN/qjs_cc.exe"

collect() {
    # $1 = label, $2 = exe native, $3 = script native
    local label="$1" exe="$2" script="$3"
    PROFCOUNT_OUT="E:/bcf/mochiberry/build/callcount/$label.raw" \
        "$exe" "$script" >/dev/null 2>&1
    if [ ! -f "$OUT/$label.raw" ]; then
        echo "!! $label: no counts file"; return 1
    fi
}

echo "==> running"
collect mb_base   "$OUT_WIN/mbjs_cc.exe" "$PROBE_BASE_WIN"
collect mb_work   "$OUT_WIN/mbjs_cc.exe" "$PROBE_WIN"
collect qjs_base  "$OUT_WIN/qjs_cc.exe"  "$PROBE_BASE_WIN"
collect qjs_work  "$OUT_WIN/qjs_cc.exe"  "$PROBE_WIN"

# NOTE: the report runs under native Python, so it needs NATIVE paths --
# an MSYS POSIX path makes os.path.exists() silently fail and the report
# comes back empty with no error at all.
python3 "$(dirname "$0")/callcount_report.py" "$OUT_WIN" "$OUT_WIN" \
    "$OUT_WIN/mbjs_cc.exe" "$OUT_WIN/qjs_cc.exe" "$MINGW/nm.exe"
