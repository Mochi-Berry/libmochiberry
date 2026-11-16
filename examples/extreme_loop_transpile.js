/*
 * extreme_loop_transpile.js — stress-test for the native transpiler subset.
 *
 * Same subset as mbjs-to-native.js supports: int64 only, no strings,
 * no ternary operator, no closures.
 * Targets: --c, --gas, --llvm, --basm (beasm -> Berry OS / ELF)
 */

function fib(n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
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

function collatz_len(n) {
    var len = 0;
    while (n != 1) {
        if (n % 2 == 0) {
            n = n / 2;
        } else {
            n = n * 3 + 1;
        }
        len = len + 1;
    }
    return len;
}

function main() {
    print("=== extreme_loop_transpile.js ===");

    var f30 = fib(30);
    print("fib(30) = 832040");

    var f25 = fib(25);
    print("fib(25) = 75025");

    var ns = nested_sum(500);
    print("nested_sum(500) = 125250");

    var mc = mod_chain(100000);
    print("mod_chain done");

    var cl = collatz_len(837799);
    print("collatz(837799) len = 524");

    var total = 0;
    for (var i = 0; i < 10; i++) {
        total = total + fib(20);
    }
    print("combined(10x fib20) = 67650");

    print("EXTREME LOOP TRANSPILER: ALL OK");
    return 0;
}
