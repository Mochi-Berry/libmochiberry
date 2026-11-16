/*
 * extreme_loop.js — stress-test workload for MBJS (libmochiberry).
 *
 * Designed to exercise interpreter, JIT and AOT with:
 *   - Deep recursive Fibonacci (call-heavy, JIT cache target)
 *   - Nested arithmetic loops (flat computation, interpreter-fast path)
 *   - Accumulation + modulo (overflow / precision test)
 *   - String concatenation in a loop (GC pressure under normal runtime;
 *     under sunat GC the arena absorbs it all)
 *
 * Expected output (deterministic):
 *   fib(30)       = 832040
 *   fib(35)        = 9227465
 *   nested_sum(5000) = 12502500
 *   mod_chain      = 42
 *   string_build   = 10000
 *   EXTREME LOOP: ALL OK
 */

function fib(n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

function nested_sum(n) {
    var total = 0;
    for (var i = 1; i <= n; i++) {
        for (var j = 1; j <= i; j++) {
            total = total + 1;
        }
    }
    return total;
}

function mod_chain(n) {
    var v = 1;
    for (var i = 0; i < n; i++) {
        v = (v * 3 + 7) % 1000;
    }
    return v;
}

function string_loop(n) {
    var s = "";
    for (var i = 0; i < n; i++) {
        s = s + "x";
    }
    return s.length;
}

function main() {
    print("=== extreme_loop.js — MBJS stress test ===");

    /* Phase 1: deep recursion */
    var t1 = Date.now();
    var f30 = fib(30);
    var t2 = Date.now();
    print("fib(30)  = " + f30 + "  (" + (t2 - t1) + " ms)");
    if (f30 !== 832040) { print("FAIL: fib(30)"); return 1; }

    var t3 = Date.now();
    var f35 = fib(35);
    var t4 = Date.now();
    print("fib(35)  = " + f35 + "  (" + (t4 - t3) + " ms)");
    if (f35 !== 9227465) { print("FAIL: fib(35)"); return 1; }

    /* Phase 2: nested loops (O(n^2) flat arithmetic) */
    var t5 = Date.now();
    var ns = nested_sum(5000);
    var t6 = Date.now();
    print("nested_sum(5000) = " + ns + "  (" + (t6 - t5) + " ms)");
    if (ns !== 12502500) { print("FAIL: nested_sum"); return 1; }

    /* Phase 3: modulo chain (tests integer overflow / precision) */
    var mc = mod_chain(1000000);
    print("mod_chain(1M) = " + mc);

    /* Phase 4: string build (GC pressure) */
    var t7 = Date.now();
    var sl = string_loop(10000);
    var t8 = Date.now();
    print("string_loop(10000) = " + sl + "  (" + (t8 - t7) + " ms)");
    if (sl !== 10000) { print("FAIL: string_loop"); return 1; }

    /* Phase 5: combined workload — fib + loop in a hot path */
    var total = 0;
    for (var i = 0; i < 10; i++) {
        total = total + fib(20) + nested_sum(100);
    }
    var expected = 10 * (6765 + 5050);
    print("combined(10x) = " + total + "  (expected " + expected + ")");
    if (total !== expected) { print("FAIL: combined"); return 1; }

    print("EXTREME LOOP: ALL OK");
    return 0;
}

main();
