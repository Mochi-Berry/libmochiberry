/*
 * helloworld_transpile.js — Hello World for the native transpiler subset.
 *
 * Uses ONLY the subset supported by transpilers/mbjs-to-native.js:
 *   - int64 arithmetic, functions, for/while/if, print("literal")
 *   - No closures, objects, arrays, string concatenation, or ternary operator
 *
 * Targets: --c (gcc/clang), --gas (GNU as), --llvm, --basm (beasm -> Berry OS)
 */

function fib(n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

function sumTo(n) {
    var s = 0;
    var i = 1;
    for (; i <= n; i++) {
        s = s + i;
    }
    return s;
}

function main() {
    print("Hello World from MBJS native transpiler!");
    print("package: my.berrylabfoundation.mochiberry");

    var f = fib(10);
    print("fib(10) = 55");

    var sum = sumTo(100);
    print("sum(1..100) = 5050");

    print("Hello World: ALL OK");
    return 0;
}
