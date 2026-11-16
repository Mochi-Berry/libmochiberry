#!/usr/bin/env bash
# bench_compare.sh — apples-to-apples MochiBerry vs QuickJS-ng vs LunaBerry.
#
# Design rules (learned the hard way on this host):
#   * INTERLEAVED: engines are run round-robin (A/B/C/A/B/C...), never one
#     engine fully then the next. Any background load (a compiler, Defender,
#     a sibling benchmark) then hits every engine equally instead of inflating
#     only whichever ran during the contention window.
#   * We report MIN as well as median: min is the least contaminated estimate
#     of true cost, median shows what a user actually sees.
#   * Process wall time is used for the end-to-end workload, and a startup
#     baseline (empty script) is measured the same way, so the two engines can
#     be compared on "execution" alone.
#
# Usage: scripts/bench_compare.sh [reps] [workload-js] [workload-lua]

set -u
REPS="${1:-5}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT_NATIVE="$(cd "$ROOT" && pwd -W 2>/dev/null || echo "$ROOT")"
# native (Windows) paths for every native exe: MSYS POSIX paths are NOT translated
MBJS="${MBJS_BIN:-$ROOT_NATIVE/build/tier-full/mbjs.strip.exe}"
QJS="${QJS_BIN:-E:/tmp/qjsng/quickjs-0.18.0/qjs.exe}"
LUNA="${LUNA_BIN:-E:/lunaberry/build-full/lunaberry.exe}"
WJS="${2:-$ROOT_NATIVE/benchmarks/bench_workload.js}"
WLU="${3:-$ROOT_NATIVE/benchmarks/bench_workload.lua}"

have() { [ -x "$1" ] || [ -f "$1" ]; }
now_ms() { echo $(( $(date +%s%N) / 1000000 )); }

# --- workload: end-to-end (process wall time, startup included) ---------------
echo "=== workload: process wall time, interleaved, reps=$REPS ==="
printf "%-12s" ""; for i in $(seq 1 $REPS); do printf " %6s" "r$i"; done; echo
run_wl() { # label, cmd...
    local label="$1"; shift
    local times=""
    printf "%-12s" "$label"
    for i in $(seq 1 $REPS); do
        local s e t
        s=$(now_ms)
        "$@" >/dev/null 2>&1
        e=$(now_ms)
        t=$((e - s))
        times="$times $t"
        printf " %6s" "$t"
    done
    echo
    echo "$times"
}

T_MB=""; T_Q=""; T_L=""
for i in $(seq 1 $REPS); do
    have "$MBJS" && { s=$(now_ms); "$MBJS" "$WJS" >/dev/null 2>&1; e=$(now_ms); T_MB="$T_MB $((e-s))"; }
    have "$QJS"  && { s=$(now_ms); "$QJS"  "$WJS" >/dev/null 2>&1; e=$(now_ms); T_Q="$T_Q $((e-s))"; }
    have "$LUNA" && { s=$(now_ms); "$LUNA" "$WLU" >/dev/null 2>&1; e=$(now_ms); T_L="$T_L $((e-s))"; }
done
echo
echo "--- raw process times (ms) ---"
[ -n "$T_MB" ] && echo "mochiberry:$T_MB"
[ -n "$T_Q" ]  && echo "quickjs-ng:$T_Q"
[ -n "$T_L" ]  && echo "lunaberry:$T_L"

medmin() { printf '%s\n' $1 | sort -g | awk '{a[NR]=$1} END{
  if (NR==0) {print "n/a"; exit}
  printf "median=%.1f min=%.1f", (NR%2)?a[(NR+1)/2]:(a[NR/2]+a[NR/2+1])/2, a[1]}'; }

echo
echo "--- startup baseline (empty script) ---"
: > E:/tmp/bench_empty.js; : > E:/tmp/bench_empty.lua
S_MB=""; S_Q=""; S_L=""
for i in $(seq 1 $REPS); do
    have "$MBJS" && { s=$(now_ms); "$MBJS" E:/tmp/bench_empty.js >/dev/null 2>&1; e=$(now_ms); S_MB="$S_MB $((e-s))"; }
    have "$QJS"  && { s=$(now_ms); "$QJS"  E:/tmp/bench_empty.js >/dev/null 2>&1; e=$(now_ms); S_Q="$S_Q $((e-s))"; }
    have "$LUNA" && { s=$(now_ms); "$LUNA" E:/tmp/bench_empty.lua >/dev/null 2>&1; e=$(now_ms); S_L="$S_L $((e-s))"; }
done
[ -n "$S_MB" ] && echo "mochiberry startup: $(medmin "$S_MB")"
[ -n "$S_Q" ]  && echo "quickjs-ng startup: $(medmin "$S_Q")"
[ -n "$S_L" ]  && echo "lunaberry  startup: $(medmin "$S_L")"

echo
echo "--- workload summary (process wall time) ---"
[ -n "$T_MB" ] && echo "mochiberry: $(medmin "$T_MB")"
[ -n "$T_Q" ]  && echo "quickjs-ng: $(medmin "$T_Q")"
[ -n "$T_L" ]  && echo "lunaberry : $(medmin "$T_L")"
