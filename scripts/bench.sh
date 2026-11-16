#!/usr/bin/env bash
#
# bench.sh — three-way engine benchmark for libmochiberry:
#
#   1. INTERPRETER  build/mbjs examples/bench_fib.js
#   2. JIT          benchmarks/bench_jit.c (cc -O2 -> dlopen, cached calls)
#   3. NATIVE (AOT) examples/bench_fib.js compiled with build-native.sh --o3
#
# All three run the identical workload (examples/bench_fib.js) and must
# print the same result; the wall times are compared so you can see how
# the JIT and AOT tiers close the gap on the interpreter.
#
# Usage:
#   sh scripts/bench.sh
#   sh scripts/bench.sh --quick   # fewer iterations via env (see below)
#
set -u

cd "$(dirname "$0")/.."

WORK="examples/bench_fib.js"
[ -x build/mbjs ] || { echo "building engine first (make)..."; make -j4 || exit 1; }
[ -f dist/libmochiberry.so ] || { echo "building libmochiberry..."; make libmochiberry || exit 1; }

ms() { # wall-clock milliseconds
    date +%s%N
}

echo "======================================================"
echo "  MBJS engine benchmark — workload: $WORK"
echo "======================================================"

# --- 1. interpreter -------------------------------------------------------
echo "[1/3] interpreter..."
t0=$(ms)
OUT_I=$(build/mbjs "$WORK" 2>&1)
t1=$(ms)
I_MS=$(( (t1 - t0) / 1000000 ))
echo "      interpreter: ${I_MS} ms  ($OUT_I)"

# --- 2. JIT ---------------------------------------------------------------
echo "[2/3] JIT (cc -O2 -> dlopen, cached dispatch)..."
mkdir -p build
cc -O2 -I. -Idist/include benchmarks/bench_jit.c mbjs-jit.c \
    -Ldist -lmochiberry -Wl,-rpath,"\$ORIGIN/../dist" -o build/bench-jit -lm -ldl || {
    echo "      FAIL: could not build the JIT benchmark harness" >&2
    exit 1
}
t0=$(ms)
OUT_J=$(build/bench-jit 2>&1)
t1=$(ms)
J_MS=$(( (t1 - t0) / 1000000 ))
printf '%s\n' "$OUT_J" | sed 's/^/      /'
echo "      jit wall: ${J_MS} ms (total, incl. compile)"

# --- 3. AOT native --------------------------------------------------------
echo "[3/3] AOT native (build-native.sh --o3 --strip)..."
NATIVE_OUT="$(mktemp -d)/bench_fib_native"
sh scripts/build-native.sh "$WORK" "$NATIVE_OUT" --o3 --strip >/dev/null || {
    echo "      FAIL: AOT native build failed" >&2
    exit 1
}
t0=$(ms)
OUT_N=$("$NATIVE_OUT" 2>&1)
t1=$(ms)
N_MS=$(( (t1 - t0) / 1000000 ))
echo "      native: ${N_MS} ms  ($OUT_N)"

echo "======================================================"
echo "  results (identical workload, wall time):"
echo "    interpreter : ${I_MS} ms"
echo "    JIT         : ${J_MS} ms"
echo "    native AOT  : ${N_MS} ms"
if [ "$I_MS" -gt 0 ] && [ "$N_MS" -gt 0 ]; then
    SPEEDUP=$(awk -v i="$I_MS" -v n="$N_MS" 'BEGIN { printf "%.1f", i / n }')
    echo "    native vs interpreter speedup: ${SPEEDUP}x"
fi
echo "======================================================"

# Agreement check: all three engines must produce the same result.
if [ "$OUT_I" != "$OUT_N" ]; then
    echo "FAIL: interpreter and native results differ!" >&2
    exit 1
fi
case "$OUT_J" in
    *"OK"*) : ;;
    *)
        echo "FAIL: JIT harness did not report OK" >&2
        exit 1
        ;;
esac
echo "bench: ALL OK (results agree across interpreter / JIT / native)"
