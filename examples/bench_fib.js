/* bench_fib.js — the shared workload for scripts/bench.sh.
 *
 * The exact same JS runs in three engines:
 *   1. the MBJS interpreter (build/mbjs examples/bench_fib.js)
 *   2. the JIT tier (benchmarks/bench_jit.c calls this logic via
 *      mbjs_jit_call() in a hot loop)
 *   3. an AOT native binary (sh scripts/build-native.sh --o3)
 *
 * Workload: a recursive Fibonacci (call-heavy, stresses the call
 * machinery) plus a flat numeric loop (stresses arithmetic). The final
 * result is printed so every engine can be checked for agreement.
 */

function fib(n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

function sumTo(n) {
    let s = 0;
    for (let i = 1; i <= n; i++) {
        s += i;
    }
    return s;
}

const ITERS = 20;
let total = 0;
for (let i = 0; i < ITERS; i++) {
    total += fib(26) + sumTo(100000);
}
print("bench total = " + total);
/* fib(26) = 121393, sumTo(100000) = 5000050000, ITERS = 20. */
if (total !== 20 * (121393 + 5000050000)) {
    print("bench FAILED: unexpected total");
    std.exit(1);
}
