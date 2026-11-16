/**
 * arm64.js — ARM64 (AArch64 / ARMv8-A) GAS assembly emitter for MBJS transpiler
 *
 * Emits GNU as-compatible ARM64 (AArch64) syntax from the MBJS AST.
 * Uses A64 fixed 32-bit instruction set.
 *
 * Target: Linux ARM64 (Apple Silicon, Graviton, Raspberry Pi 4/5), Berry OS ARM64
 * Calling convention: AAPCS64 (x0-x7 args, x0-x1 return, x29=FP, x30=LR, SP)
 *
 * Copyright (c) 2024-2026 Berry Lab Foundation — libmochiberry project
 */

"use strict";

import { ARM64 } from "./isa_reference.js";

/* ------------------------------------------------------------------ */
/* ARM64 GAS Assembly Emitter                                          */
/* ------------------------------------------------------------------ */

/**
 * Emit ARM64 GAS assembly from MBJS AST
 * @param {object} ast - Parsed AST from mbjs-to-native.js parser
 * @returns {string} GAS assembly source (AArch64)
 */
export function emit_arm64_gas(ast) {
    const locals = new Map();
    const fname = (name) => "fn_" + sym(name);
    const strings = printStrings(ast);
    let stack = 0;
    let label = 0;
    const L = () => ".L" + (label++);

    function sym(name) { return name.replace(/[^a-zA-Z0-9_]/g, "_"); }

    function alloc(name) {
        if (!locals.has(name)) {
            stack += 8;
            locals.set(name, stack);
        }
        return locals.get(name);
    }

    function expr(e, out) {
        switch (e.kind) {
        case "num": {
            // Efficient immediate loading
            const v = BigInt(e.value);
            if (v >= 0n && v <= 0xFFFFn) {
                out.push("\tmov x0, #" + v);
            } else if (v >= -0x8000n && v < 0n) {
                out.push("\tmov x0, #" + v);
            } else {
                out.push("\tmov x0, #0x" + v.toString(16));
            }
            return;
        }
        case "var":
            out.push("\tldr x0, [x29, #-" + alloc(e.name) + "]");
            return;
        case "postinc":
        case "postdec":
            out.push("\tldr x0, [x29, #-" + alloc(e.name) + "]");
            out.push("\t" + (e.op === "++" ? "add" : "sub") + " x0, x0, #1");
            out.push("\tstr x0, [x29, #-" + alloc(e.name) + "]");
            return;
        case "call":
            if (e.name === "print") throw new Error("print() must be a statement");
            if (e.name === "sys_exit") {
                expr(e.args[0], out);
                out.push("\tmov x8, #93");    // SYS_exit (ARM64)
                out.push("\tsvc #0");
                return;
            }
            if (e.name === "putc") {
                expr(e.args[0], out);
                out.push("\tsub sp, sp, #16");
                out.push("\tstrb w0, [sp]");
                out.push("\tmov x0, #1");     // fd = stdout
                out.push("\tmov x1, sp");     // buf
                out.push("\tmov x2, #1");     // len = 1
                out.push("\tmov x8, #64");    // SYS_write
                out.push("\tsvc #0");
                out.push("\tadd sp, sp, #16");
                return;
            }
            // Push args (AAPCS64: x0-x7)
            for (let i = Math.min(e.args.length - 1, 7); i >= 0; i--) {
                expr(e.args[i], out);
                out.push("\tstr x0, [sp, #-" + ((i + 1) * 8) + "]!");
            }
            out.push("\tbl " + fname(e.name));
            // Pop args
            for (let i = 0; i < Math.min(e.args.length, 8); i++)
                out.push("\tldr x" + i + ", [sp], #8");
            return;
        case "unop":
            expr(e.a, out);
            if (e.op === "-") {
                out.push("\tneg x0, x0");
            } else {
                out.push("\tcmp x0, #0");
                out.push("\tmov x0, #0");
                out.push("\tcset x0, eq");
            }
            return;
        case "binop": {
            if (e.op === "&&" || e.op === "||") {
                const T = L(), D = L(), E = L();
                expr(e.a, out);
                out.push("\tcmp x0, #0");
                out.push(e.op === "&&" ? "\tbeq " + E : "\tbne " + T);
                expr(e.b, out);
                out.push("\tcmp x0, #0");
                out.push(e.op === "&&" ? "\tbeq " + E : "\tbne " + T);
                out.push("\tmov x0, #1");
                out.push("\tb " + D);
                out.push(E + ":");
                out.push("\tmov x0, #0");
                out.push(D + ":");
                return;
            }
            expr(e.b, out);
            out.push("\tstr x0, [sp, #-16]!");  // push with alignment
            expr(e.a, out);
            out.push("\tldr x1, [sp], #16");
            // x0 = a, x1 = b
            switch (e.op) {
            case "+": out.push("\tadd x0, x0, x1"); break;
            case "-": out.push("\tsub x0, x0, x1"); break;
            case "*": out.push("\tmul x0, x0, x1"); break;
            case "/":
                out.push("\tsdiv x0, x0, x1");
                break;
            case "%":
                out.push("\tsdiv x2, x0, x1");
                out.push("\tmsub x0, x2, x1, x0"); // x0 = x0 - x2*x1
                break;
            default: {
                const cc = { "<": "lt", "<=": "le", ">": "gt", ">=": "ge",
                             "==": "eq", "!=": "ne" }[e.op];
                if (!cc) throw new Error("arm64: unsupported binop " + e.op);
                out.push("\tcmp x0, x1");
                out.push("\tmov x0, #0");
                out.push("\tcset x0, " + cc);
                break;
            }
            }
            return;
        }
        default: throw new Error("arm64: unsupported expr " + e.kind);
        }
    }

    function stmts(list, out) {
        for (const s of list) {
            switch (s.kind) {
            case "assign":
                alloc(s.name);
                if (s.value === null)
                    out.push("\tmov x0, #0");
                else
                    expr(s.value, out);
                out.push("\tstr x0, [x29, #-" + locals.get(s.name) + "]");
                break;
            case "block": stmts(s.stmts, out); break;
            case "if": {
                const E = L(), X = L();
                expr(s.cond, out);
                out.push("\tcmp x0, #0");
                out.push("\tbeq " + (s.els ? X : E));
                stmts([s.then], out);
                if (s.els) {
                    out.push("\tb " + E);
                    out.push(X + ":");
                    stmts([s.els], out);
                }
                out.push(E + ":");
                break;
            }
            case "while": {
                const T = L(), E = L();
                out.push(T + ":");
                expr(s.cond, out);
                out.push("\tcmp x0, #0");
                out.push("\tbeq " + E);
                stmts([s.body], out);
                out.push("\tb " + T);
                out.push(E + ":");
                break;
            }
            case "for": {
                if (s.init && s.init.kind === "assign") {
                    alloc(s.init.name);
                    expr(s.init.value, out);
                    out.push("\tstr x0, [x29, #-" + locals.get(s.init.name) + "]");
                } else if (s.init) expr(s.init, out);
                const T = L(), E = L();
                out.push(T + ":");
                if (s.cond) {
                    expr(s.cond, out);
                    out.push("\tcmp x0, #0");
                    out.push("\tbeq " + E);
                }
                stmts([s.body], out);
                if (s.upd) expr(s.upd, out);
                out.push("\tb " + T);
                out.push(E + ":");
                break;
            }
            case "return":
                if (s.value) expr(s.value, out);
                else out.push("\tmov x0, #0");
                out.push("\tmov sp, x29");
                out.push("\tldp x29, x30, [sp], #16");
                out.push("\tret");
                return;
            case "expr": {
                const lit = printLiteral(s.expr);
                if (lit !== null) {
                    const idx = strings.indexOf(lit);
                    out.push("\tadrp x0, str_" + idx);
                    out.push("\tadd x0, x0, :lo12:str_" + idx);
                    out.push("\tmov x1, #" + lit.length);
                    out.push("\tbl io_puts");
                } else {
                    expr(s.expr, out);
                }
                break;
            }
            default: throw new Error("arm64: unsupported stmt " + s.kind);
            }
        }
    }

    // --- Code generation ---
    const out = [];
    out.push("// generated by mbjs-to-native.js (--gas-arm64) — AArch64");
    out.push("// target: Linux ARM64 (Apple Silicon / Graviton / Cortex-A)");
    out.push("// calling convention: AAPCS64 (x0-x7 args, x29=FP, x30=LR)");
    out.push("");

    if (strings.length) {
        out.push(".section .rodata");
        strings.forEach((s, idx) => {
            out.push("str_" + idx + ":");
            out.push("\t.asciz \"" + escapeStr(s) + "\"");
        });
        out.push("");
    }

    out.push(".section .text");
    out.push("");

    // I/O helpers
    out.push("// Freestanding I/O hooks — ARM64 Linux syscall ABI");
    out.push("// io_puts: write string (x0=ptr, x1=len)");
    out.push("io_puts:");
    out.push("\tmov x2, x1");
    out.push("\tmov x1, x0");
    out.push("\tmov x0, #1");       // fd = stdout
    out.push("\tmov x8, #64");      // SYS_write
    out.push("\tsvc #0");
    out.push("\tret");
    out.push("");

    // Functions
    for (const f of ast.funcs) {
        locals.clear();
        stack = 0;
        collectLocals(f).forEach(n => alloc(n));
        const frame = (stack + 15) & ~15; // 16-byte aligned

        out.push(fname(f.name) + ":");
        out.push("\tstp x29, x30, [sp, #" + (-frame - 16) + "]!");
        out.push("\tmov x29, sp");

        for (let i = 0; i < f.params.length && i < 8; i++) {
            out.push("\tstr x" + i + ", [x29, #-" + alloc(f.params[i]) + "]");
        }

        stmts(f.body, out);

        out.push("\tmov x0, #0");
        out.push("\tldp x29, x30, [sp], #" + (frame + 16));
        out.push("\tret");
        out.push("");
    }

    // Entry point
    out.push(".globl _start");
    out.push("_start:");
    out.push("\tbl fn_main");
    out.push("\tmov x8, #93");    // SYS_exit
    out.push("\tsvc #0");
    out.push("");

    return out.join("\n") + "\n";
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

function printStrings(ast) {
    const out = [];
    (function walk(node) {
        if (node && typeof node === "object") {
            if (node.kind === "call" && node.name === "print" &&
                node.args.length === 1 && node.args[0].kind === "str")
                out.push(node.args[0].value);
            for (const k of Object.keys(node)) {
                const v = node[k];
                if (Array.isArray(v)) v.forEach(walk);
                else walk(v);
            }
        }
    })(ast);
    return [...new Set(out)];
}

function printLiteral(e) {
    if (e.kind === "call" && e.name === "print" &&
        e.args.length === 1 && e.args[0].kind === "str")
        return e.args[0].value;
    return null;
}

function collectLocals(func) {
    const names = [];
    const seen = new Set();
    const add = (n) => { if (!seen.has(n)) { seen.add(n); names.push(n); } };
    func.params.forEach(add);
    (function walk(node) {
        if (!node || typeof node !== "object") return;
        if (node.kind === "assign" || node.kind === "postinc" ||
            node.kind === "postdec" || node.kind === "var")
            add(node.name);
        for (const k of Object.keys(node)) {
            const v = node[k];
            if (Array.isArray(v)) v.forEach(walk);
            else walk(v);
        }
    })(func.body);
    return names;
}

function escapeStr(s) {
    return s.replace(/\\/g, "\\\\").replace(/"/g, '\\"')
            .replace(/\n/g, "\\n").replace(/\t/g, "\\t");
}
