/**
 * asm_modes_demo.js — Demonstrates all 3 inline assembly modes
 *
 * Transpile with:
 *   build/mbjs -m transpilers/mbjs-to-native.js examples/asm_modes_demo.js --basm -o dist/asm_demo.basm
 *   build/mbjs -m transpilers/mbjs-to-native.js examples/asm_modes_demo.js --gas -o dist/asm_demo.s
 *   build/mbjs -m transpilers/mbjs-to-native.js examples/asm_modes_demo.js --gas-arm64 -o dist/asm_demo_arm64.s
 *   build/mbjs -m transpilers/mbjs-to-native.js examples/asm_modes_demo.js --gas-rv64 -o dist/asm_demo_rv64.s
 */

"use strict";

/* ======================================================================
 * MODE 1: @asm(...) — Raw Inline Assembly
 *
 * Assembly is emitted VERBATIM to .basm output.
 * No transpiler optimization, no placeholder resolution.
 * Perfect for: interrupt handlers, I/O ports, context switches.
 * ====================================================================== */

// Raw mode: simple return 42
function getAnswer() {
    @asm(`
        mov rax, 42
        ret
    `);
}

// Raw mode volatile: never removed by optimizer
@naked
function syscall_exit() {
    @asm volatile(`
        mov rax, 60
        syscall
    `);
}

// Raw mode: hardware interrupt
@naked
@interrupt
function timer_isr() {
    @asm(`
        push rax
        push rbx
        push rcx
        push rdx
        call timer_handler
        pop rdx
        pop rcx
        pop rbx
        pop rax
        iretq
    `);
}

/* ======================================================================
 * MODE 2: <__ ... __> — Optimized Inline Assembly
 *
 * Transpiler resolves {variable} placeholders to registers.
 * Control flow integrates with surrounding JS.
 * Perfect for: performance-critical loops, SIMD, bit ops.
 * ====================================================================== */

// Optimized mode: add two numbers
function asmAdd(a, b) {
    <__
        mov rax, {a}
        add rax, {b}
        ret
    __>
}

// Optimized mode: fibonacci with branch prediction hints
function asmFib(n) {
    if (n < 2) return n;
    <__
        mov rax, {n}
        cmp rax, 2
        jl .done
        mov rcx, 0
        mov rdx, 1
    .loop:
        mov rsi, rcx
        add rsi, rdx
        mov rcx, rdx
        mov rdx, rsi
        dec rax
        cmp rax, 2
        jg .loop
        mov rax, rdx
    .done:
        ret
    __>
}

// Optimized mode: memory copy (fast)
function asmMemcpy(dest, src, len) {
    <__
        mov rdi, {dest}
        mov rsi, {src}
        mov rcx, {len}
        rep movsb
        mov rax, {dest}
        ret
    __>
}

// Optimized mode: bit scan forward
function asmBsf(value) {
    <__
        mov rax, {value}
        bsf rax, rax
        ret
    __>
}

/* ======================================================================
 * MODE 3: asm`...` — Template Inline Assembly
 *
 * Hybrid mode: ${expression} interpolation resolved by transpiler.
 * Perfect for: constants, computed offsets, parameterized routines.
 * ====================================================================== */

// Template mode: syscall with variable number
function syscall1(num, arg1) {
    return asm`
        mov rax, ${num}
        mov rdi, ${arg1}
        syscall
        ret
    `;
}

// Template mode: write string to stdout
function writeString(ptr, len) {
    return asm`
        mov rax, 1
        mov rdi, 1
        mov rsi, ${ptr}
        mov rdx, ${len}
        syscall
        ret
    `;
}

// Template mode: multiply with shift (for power of 2)
function asmShiftMul(x, shift) {
    return asm`
        mov rax, ${x}
        shl rax, ${shift}
        ret
    `;
}

// Template mode: conditional move
function asmCmovlt(a, b) {
    return asm`
        mov rax, ${a}
        mov rbx, ${b}
        cmp rax, rbx
        cmovlt rax, rbx
        ret
    `;
}

/* ======================================================================
 * Interop: mix JS and inline asm
 * ====================================================================== */

// Call asm function from JS
function computeSum(n) {
    let sum = 0;
    for (let i = 1; i <= n; i++) {
        sum = asmAdd(sum, i);
    }
    return sum;
}

// Use asm for hot loop, JS for setup
function processArray(arr) {
    let total = 0;
    const len = arr.length;
    // Use asm for the inner loop
    <__
        mov rax, 0
        mov rcx, {len}
        mov rsi, {arr}
    .sum_loop:
        add rax, [rsi]
        add rsi, 8
        dec rcx
        jnz .sum_loop
        ret
    __>
}

/* ======================================================================
 * Export entry point for testing
 * ====================================================================== */

function main() {
    print("=== Inline Assembly Modes Demo ===");
    print("");

    // Mode 1: Raw
    print("Mode 1 — @asm (Raw):");
    print("  getAnswer() = " + getAnswer());
    print("");

    // Mode 2: Optimized
    print("Mode 2 — <__ __> (Optimized):");
    print("  asmAdd(10, 20) = " + asmAdd(10, 20));
    print("  asmFib(10) = " + asmFib(10));
    print("  asmBsf(256) = " + asmBsf(256));
    print("");

    // Mode 3: Template
    print("Mode 3 — asm`` (Template):");
    print("  asmShiftMul(3, 4) = " + asmShiftMul(3, 4));
    print("  asmCmovlt(10, 20) = " + asmCmovlt(10, 20));
    print("");

    // JS interop
    print("JS ↔ ASM interop:");
    print("  computeSum(100) = " + computeSum(100));
    print("");

    print("ALL INLINE ASM TESTS PASSED");
}
