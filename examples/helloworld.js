/*
 * helloworld.js — Hello World for MBJS (libmochiberry).
 *
 * Runs in all four pipelines:
 *   1. Interpreter:  build/mbjs examples/helloworld.js
 *   2. JIT:          bench_jit.c calls this via mbjs_jit_compile()
 *   3. AOT native:   sh scripts/build-native.sh examples/helloworld.js
 *   4. BeASM:        transpile to .basm, assemble to .befb (Berry OS) / ELF
 */

function fib(n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

function main() {
    print("Hello World from MBJS (libmochiberry)!");
    print("package: my.berrylabfoundation.mochiberry");
    print("ECMAScript level: 2026");

    /* quick fib sanity */
    var f10 = fib(10);
    print("fib(10) = " + f10);

    var f20 = fib(20);
    print("fib(20) = " + f20);

    /* arithmetic check */
    var sum = 0;
    for (var i = 1; i <= 1000; i++) {
        sum = sum + i;
    }
    print("sum(1..1000) = " + sum);

    print("Hello World: ALL OK");
    return 0;
}

main();
