#!/usr/bin/env bash
# bench_tiers.sh — build & run the three-way tier benchmark (interp / jit / aot).
#
# Cross-platform: Linux/macOS (gcc + .so + dlopen) and MSYS2/Windows
# (mingw-w64 gcc + .dll). Produces a single comparable table:
#
#   interp   bytecode interpreter
#   jit      CC-backend JIT shim (cold = compiler runs, warm = disk cache)
#   aot      AOT bytecode embed (mbjsc -e -> standalone C app)
#
# Usage: scripts/bench_tiers.sh [tier-dir] [iterations]
#   tier-dir    build/tier-full by default
#   iterations  repetitions per tier, median reported (default 3)

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# MinGW gcc/ar/exe cannot open MSYS POSIX paths ("/e/bcf/..."). Native
# programs need the "E:/bcf/..." form; bash itself keeps the POSIX form.
ROOT_NATIVE="$(cd "$ROOT" && pwd -W 2>/dev/null || echo "$ROOT")"
TIER="${1:-$ROOT/build/tier-full}"
TIER_NATIVE="$(cd "$TIER" 2>/dev/null && pwd -W 2>/dev/null || echo "$TIER")"
ITER="${2:-3}"
WORK="$ROOT/build/bench-tiers"

if [ ! -d "$TIER" ]; then
    echo "error: tier dir not found: $TIER" >&2
    exit 2
fi

# ---- locate toolchain -------------------------------------------------------
CC="${CC:-}"
if [ -z "$CC" ]; then
    for c in cc gcc clang; do
        if command -v "$c" >/dev/null 2>&1; then CC="$c"; break; fi
    done
fi
if [ -z "$CC" ]; then
    echo "error: no C compiler found (set CC=...)" >&2
    exit 2
fi
# Native (Windows) path for the compiler: MSYS POSIX paths ("/c/...") are NOT
# translated for native programs on this host, so gcc would fail with
# "No such file or directory" even though the file exists.
if command -v cygpath >/dev/null 2>&1; then
    CC="$(cygpath -m "$CC" 2>/dev/null || echo "$CC")"
fi

MINGW=""
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        MINGW=1
        EXEEXT=".exe"
        LIBEXT=".dll"
        ;;
    *)
        EXEEXT=""
        LIBEXT=".so"
        ;;
esac

mkdir -p "$WORK"
WORK_NATIVE="$(cd "$WORK" && pwd -W 2>/dev/null || echo "$WORK")"

# ---- build the harness ------------------------------------------------------
echo "==> building bench_tiers (CC=$CC, $(uname -s))"
"$CC" -O2 -I"$ROOT_NATIVE" -I"$ROOT_NATIVE/dist/include" \
    "$ROOT_NATIVE/benchmarks/bench_tiers.c" \
    -Wl,--start-group \
      "$TIER_NATIVE/libmbjs.a" "$TIER_NATIVE/mbjs-libc.o" "$TIER_NATIVE/mbts.o" \
    -Wl,--end-group \
    -lm -Wl,--export-all-symbols \
    -o "$WORK_NATIVE/bench_tiers$EXEEXT" || {
        echo "error: harness build failed" >&2
        exit 3
    }

# ---- AOT tier: mbjsc -b -> raw bytecode, fed to mbjs_std_eval_binary ---------
# This is the *real* AOT path: the script is compiled ahead of time into a
# bytecode blob, so start-up skips parsing and compiling entirely.
MBJSC="$TIER_NATIVE/mbjsc$EXEEXT"
[ -x "$MBJSC" ] || MBJSC="$TIER_NATIVE/mbjsc"
AOT_BIN="$WORK/bench_aot$EXEEXT"
AOT_BIN_NATIVE="$WORK_NATIVE/bench_aot$EXEEXT"
AOT_BC="$WORK/bench_workload.bin"
AOT_BC_NATIVE="$WORK_NATIVE/bench_workload.bin"
if [ -x "$MBJSC" ]; then
    echo "==> compiling workload to bytecode via mbjsc -b"
    # mbjsc only scans options while the arg starts with '-'; anything after the
    # input file is treated as another input file. So: -b -o FILE, THEN the file.
    ( cd "$ROOT" && "$MBJSC" -b -o "$AOT_BC_NATIVE" \
        "$ROOT_NATIVE/benchmarks/bench_workload.js" ) || {
        echo "warn: mbjsc -b failed, skipping aot tier" >&2
        rm -f "$AOT_BC"
    }
    if [ -f "$AOT_BC" ]; then
        "$CC" -O2 -I"$ROOT_NATIVE" -I"$ROOT_NATIVE/dist/include" \
            "$ROOT_NATIVE/benchmarks/bench_aot.c" \
            -Wl,--start-group \
              "$TIER_NATIVE/libmbjs.a" "$TIER_NATIVE/mbjs-libc.o" \
              "$TIER_NATIVE/mbts.o" \
            -Wl,--end-group \
            -lm -Wl,--export-all-symbols \
            -o "$AOT_BIN_NATIVE" || {
            echo "warn: AOT link failed, skipping aot tier" >&2
            rm -f "$AOT_BIN"
        }
    fi
fi

# ---- JIT cache dir (persistent, enables the warm measurement) ---------------
JIT_CACHE="$WORK/jit-cache"
mkdir -p "$JIT_CACHE"
# The JIT dlopen()s "<cache_dir>/<key>.dll". On Windows that needs a native
# path -- an MSYS POSIX path silently turns every lookup into a cache miss.
JIT_CACHE_NATIVE="$(cd "$JIT_CACHE" && pwd -W 2>/dev/null || echo "$JIT_CACHE")"

# These are consumed by the *native* compiler when the JIT shim is built,
# so they must be native ("E:/...") paths, not MSYS POSIX paths.
export MBJS_JIT_INC_DIR="$ROOT_NATIVE/dist/include"
export MBJS_JIT_LIB_DIR="$ROOT_NATIVE/dist"
if [ -n "${MBJS_JIT_CC:-}" ]; then
    :
else
    MBJS_JIT_CC="$(command -v "$CC" 2>/dev/null || echo "$CC")"
fi
# The engine spawns the compiler through system() -> cmd.exe, which does NOT
# understand MSYS POSIX paths ("/c/Users/..."). A POSIX path there makes the
# probe fail ("C compiler ... is not usable") and the JIT silently degrades to
# the interpreter -- the run then LOOKS like a successful jit-cold/jit-warm
# measurement while nothing was compiled. Always hand it a native path.
if command -v cygpath >/dev/null 2>&1; then
    MBJS_JIT_CC="$(cygpath -m "$MBJS_JIT_CC" 2>/dev/null || echo "$MBJS_JIT_CC")"
fi
export MBJS_JIT_CC

# ---- run --------------------------------------------------------------------
run_set() {
    label="$1"; shift
    best=""; times=""
    i=1
    while [ "$i" -le "$ITER" ]; do
        out="$("$@" 2>&1)"
        rc=$?
        if [ $rc -ne 0 ]; then
            echo "  $label run $i FAILED rc=$rc"
            echo "$out" | sed 's/^/    /'
            return 1
        fi
        t="$(printf '%s\n' "$out" | sed -n 's/.*TIME_MS \([0-9.]*\).*/\1/p' | tail -1)"
        r="$(printf '%s\n' "$out" | sed -n 's/^RESULT \([0-9-]*\).*/\1/p; s/^BENCH_RESULT \([0-9-]*\).*/\1/p' | tail -1)"
        if [ -z "$t" ]; then
            echo "  $label run $i: no TIME_MS"
            echo "$out" | sed 's/^/    /'
            return 1
        fi
        times="$times $t"
        echo "  $label run $i: ${t} ms (result=$r)"
        i=$((i+1))
    done
    # median of the collected times
    med="$(printf '%s\n' $times | sort -g | awk '{a[NR]=$1} END{
        if (NR%2) print a[(NR+1)/2]; else printf "%.1f", (a[NR/2]+a[NR/2+1])/2 }')"
    LAST_TIMES="$times"
    return 0
}

RESULTS=""
echo "==> benchmark (iterations=$ITER)"
run_set interp "$WORK/bench_tiers$EXEEXT" interp big || true
INTERP_TIMES="${LAST_TIMES:-}"
# interp_all = parse + compile + execute (the real cold-path cost)
run_set interp_all "$WORK/bench_tiers$EXEEXT" interp_all big || true
INTERP_ALL_TIMES="${LAST_TIMES:-}"

# JIT cold: fresh cache dir each iteration so cc must run
echo "-- JIT cold (compiler runs every time)"
i=1
while [ "$i" -le "$ITER" ]; do
    rm -rf "$JIT_CACHE"; mkdir -p "$JIT_CACHE"
    out="$(MBJS_JIT_CACHE_DIR="$JIT_CACHE_NATIVE" "$WORK/bench_tiers$EXEEXT" jit big 2>&1)"
    t="$(printf '%s\n' "$out" | sed -n 's/.*TIME_MS \([0-9.]*\).*/\1/p' | tail -1)"
    echo "  jit-cold run $i: ${t:-n/a} ms"
    printf '%s\n' "$out" | grep -E '^JITSTATS' | sed 's/^/    /'
    [ "$i" -eq 1 ] && COLD_TIMES="$t" || COLD_TIMES="$COLD_TIMES $t"
    i=$((i+1))
done

# JIT warm: cache populated once, then reused
echo "-- JIT warm (persistent .dll cache)"
rm -rf "$JIT_CACHE"; mkdir -p "$JIT_CACHE"
MBJS_JIT_CACHE_DIR="$JIT_CACHE_NATIVE" "$WORK/bench_tiers$EXEEXT" jit big >/dev/null 2>&1
WARM_TIMES=""
i=1
while [ "$i" -le "$ITER" ]; do
    out="$(MBJS_JIT_CACHE_DIR="$JIT_CACHE_NATIVE" "$WORK/bench_tiers$EXEEXT" jit big 2>&1)"
    t="$(printf '%s\n' "$out" | sed -n 's/.*TIME_MS \([0-9.]*\).*/\1/p' | tail -1)"
    echo "  jit-warm run $i: ${t:-n/a} ms"
    printf '%s\n' "$out" | grep -E '^JITSTATS' | sed 's/^/    /'
    [ "$i" -eq 1 ] && WARM_TIMES="$t" || WARM_TIMES="$WARM_TIMES $t"
    i=$((i+1))
done

if [ -x "$AOT_BIN" ] && [ -f "$AOT_BC" ]; then
    # bench_aot is a native program -> it needs a native path for the blob
    run_set aot "$AOT_BIN" "$AOT_BC_NATIVE" || true
    AOT_TIMES="${LAST_TIMES:-}"
fi

# ---- summary ----------------------------------------------------------------
echo
echo "================ SUMMARY (median of $ITER) ================"
med_of() { printf '%s\n' $1 | sort -g | awk '{a[NR]=$1} END{
        if (NR%2) printf "%.1f", a[(NR+1)/2]; else printf "%.1f", (a[NR/2]+a[NR/2+1])/2 }'; }
printf "interp      %8s ms   (execution only)\n" "$(med_of "$INTERP_TIMES")"
printf "interp_all  %8s ms   (parse + compile + execute = cold path)\n" "$(med_of "$INTERP_ALL_TIMES")"
printf "jit-cold %8s ms   (cc runs each time)\n" "$(med_of "$COLD_TIMES")"
printf "jit-warm %8s ms   (persistent cache, no cc)\n" "$(med_of "$WARM_TIMES")"
[ -x "$AOT_BIN" ] && printf "aot      %8s ms   (precompiled bytecode, no parsing)\n" "$(med_of "$AOT_TIMES")"
echo "============================================================"
echo "note: the CC-backend JIT shim is C that calls MBJS_Eval + MBJS_Call."
echo "      It is compile-once/dispatch-cached, NOT a native JS codegen."
echo "      jit-warm ~= interp is the expected steady-state result."
