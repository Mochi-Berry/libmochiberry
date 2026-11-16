/* aot_demo.js — small freestanding-friendly program for the JS->native
 * transpilers (mbjs-to-native.js). Stays in the documented subset:
 * integers, functions, if/while/for, print("...") literals, putc, sys_exit.
 * Usage: build/mbjs transpilers/mbjs-to-native.js examples/aot_demo.js --c -o out.c
 */
function main() {
    print("mochiberry AOT demo");
    let total = 0;
    for (let i = 1; i <= 10; i++) {
        total = total + i;
    }
    print("sum 1..10 = ");
    print_num(total);
    let fib = fib(20);
    print("fib(20) = ");
    print_num(fib);
    if (total == 55) {
        print("sum check OK");
    } else {
        print("sum check FAILED");
    }
    let n = 0;
    while (n < 3) {
        putc(65 + n);   /* 'A' 'B' 'C' */
        n = n + 1;
    }
    putc(10);
    print("bye");
    sys_exit(0);
    return 0;
}

function print_num(x) {
    /* naive decimal printer using putc (no libc) */
    if (x < 0) { putc(45); x = -x; }
    if (x >= 10) print_num(x / 10);
    putc(48 + x % 10);
}

function fib(n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}
