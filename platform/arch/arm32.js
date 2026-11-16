/**
 * arm32.js — ARM32 (ARMv7-A/R) GAS assembly emitter for MBJS transpiler
 *
 * Emits GNU as-compatible ARM32 AT&T syntax from the MBJS AST.
 * Supports A32 (ARM) and T16/T32 (Thumb-2) instruction sets.
 *
 * Target: baremetal Berry OS, Linux ARM, embedded ARM Cortex-A/R
 * Calling convention: AAPCS (r0-r3 args, r0 return, r13=SP, r14=LR)
 *
 * Copyright (c) 2024-2026 Berry Lab Foundation — libmochiberry project
 */

"use strict";

import { ARM32 } from "./isa_reference.js";

/* ------------------------------------------------------------------ */
/* ARM32 GAS Assembly Emitter                                          */
/* ------------------------------------------------------------------ */

/**
 * Emit ARM32 GAS assembly from MBJS AST
 * @param {object} ast - Parsed AST from mbjs-to-native.js parser
 * @param {object} opts - { thumb: use Thumb-2 mode, cortex: target MCU }
 * @returns {string} GAS assembly source
 */
export function emit_arm32_gas(ast, opts = {}) {
    const locals = new Map();
    const fname = (name) => "fn_" + sym(name);
    const strings = printStrings(ast);
    let stack = 0;
    let label = 0;
    const L = () => ".L" + (label++);

    function sym(name) { return name.replace(/[^a-zA-Z0-9_]/g, "_"); }

    function alloc(name) {
        if (!locals.has(name)) {
            stack += 4;
            locals.set(name, stack);
        }
        return locals.get(name);
    }

    function expr(e, out) {
        switch (e.kind) {
        case "num":
            out.push("\tmov r0, #" + e.value);
            return;
        case "var":
            out.push("\tldr r0, [fp, #-" + alloc(e.name) + "]");
            return;
        case "postinc":
        case "postdec":
            out.push("\tldr r0, [fp, #-" + alloc(e.name) + "]");
            out.push("\t" + (e.op === "++" ? "add" : "sub") + " r0, r0, #1");
            out.push("\tstr r0, [fp, #-" + alloc(e.name) + "]");
            return;
        case "call":
            if (e.name === "print") throw new Error("print() must be a statement");
            if (e.name === "sys_exit") {
                expr(e.args[0], out);
                out.push("\tmov r7, #1");    // SYS_exit
                out.push("\tmov r0, r0");    // exit code in r0
                out.push("\tswi #0");
                return;
            }
            if (e.name === "putc") {
                expr(e.args[0], out);
                out.push("\tpush {r0}");     // put char on stack
                out.push("\tmov r0, #1");    // fd = stdout
                out.push("\tmov r1, sp");    // buf = stack
                out.push("\tmov r2, #1");    // len = 1
                out.push("\tmov r7, #4");    // SYS_write
                out.push("\tswi #0");
                out.push("\tpop {r0}");
                return;
            }
            // Push args (AAPCS: r0-r3)
            for (let i = Math.min(e.args.length - 1, 3); i >= 0; i--) {
                expr(e.args[i], out);
                out.push("\tpush {r0}");
            }
            out.push("\tbl " + fname(e.name));
            // Pop args back
            for (let i = 0; i < Math.min(e.args.length, 4); i++)
                out.push("\tpop {r" + (i) + "}");
            return;
        case "unop":
            expr(e.a, out);
            if (e.op === "-") {
                out.push("\tneg r0, r0");
            } else {
                out.push("\tcmp r0, #0");
                out.push("\tmovne r0, #0");
                out.push("\tmoveq r0, #1");
            }
            return;
        case "binop": {
            if (e.op === "&&" || e.op === "||") {
                const T = L(), D = L(), E = L();
                expr(e.a, out);
                out.push("\tcmp r0, #0");
                out.push(e.op === "&&" ? "\tbeq " + E : "\tbne " + T);
                expr(e.b, out);
                out.push("\tcmp r0, #0");
                out.push(e.op === "&&" ? "\tbeq " + E : "\tbne " + T);
                out.push("\tmov r0, #1");
                out.push("\tb " + D);
                out.push(E + ":");
                out.push("\tmov r0, #0");
                out.push(D + ":");
                return;
            }
            expr(e.b, out);
            out.push("\tpush {r0}");
            expr(e.a, out);
            out.push("\tpop {r1}");
            // r0 = a, r1 = b
            switch (e.op) {
            case "+": out.push("\tadd r0, r0, r1"); break;
            case "-": out.push("\tsub r0, r0, r1"); break;
            case "*": out.push("\tmul r0, r0, r1"); break;
            case "/":
                out.push("\tsdiv r0, r0, r1"); // ARMv7 integer divide
                break;
            case "%":
                out.push("\tsdiv r2, r0, r1");
                out.push("\tmla r0, r2, r1, r0"); // r0 = r2*r1 + r0 (remainder)
                out.push("\tsub r0, r0, r2, lsl #0"); // r0 = r0 - r2*r1
                break;
            default: {
                const cc = { "<": "lt", "<=": "le", ">": "gt", ">=": "ge",
                             "==": "eq", "!=": "ne" }[e.op];
                if (!cc) throw new Error("arm32: unsupported binop " + e.op);
                out.push("\tcmp r0, r1");
                out.push("\tmov r0, #0");
                out.push("\tmov" + cc + " r0, #1");
                break;
            }
            }
            return;
        }
        default: throw new Error("arm32: unsupported expr " + e.kind);
        }
    }

    function stmts(list, out) {
        for (const s of list) {
            switch (s.kind) {
            case "assign":
                alloc(s.name);
                if (s.value === null)
                    out.push("\tmov r0, #0");
                else
                    expr(s.value, out);
                out.push("\tstr r0, [fp, #-" + locals.get(s.name) + "]");
                break;
            case "block": stmts(s.stmts, out); break;
            case "if": {
                const E = L(), X = L();
                expr(s.cond, out);
                out.push("\tcmp r0, #0");
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
                out.push("\tcmp r0, #0");
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
                    out.push("\tstr r0, [fp, #-" + locals.get(s.init.name) + "]");
                } else if (s.init) expr(s.init, out);
                const T = L(), E = L();
                out.push(T + ":");
                if (s.cond) {
                    expr(s.cond, out);
                    out.push("\tcmp r0, #0");
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
                else out.push("\tmov r0, #0");
                out.push("\tmov sp, fp");
                out.push("\tpop {fp, pc}");
                return;
            case "expr": {
                const lit = printLiteral(s.expr);
                if (lit !== null) {
                    const idx = strings.indexOf(lit);
                    out.push("\tldr r0, =str_" + idx);
                    out.push("\tmov r1, #" + lit.length);
                    out.push("\tbl io_puts");
                } else {
                    expr(s.expr, out);
                }
                break;
            }
            default: throw new Error("arm32: unsupported stmt " + s.kind);
            }
        }
    }

    // --- Code generation ---
    const out = [];
    out.push("@ generated by mbjs-to-native.js (--gas-arm32) — ARMv7-A/R");
    out.push("@ target: " + (opts.cortex || "cortex-a9"));
    out.push("@ calling convention: AAPCS (r0-r3 args, r13=SP, r14=LR)");
    out.push("");

    // Data section: string constants
    if (strings.length) {
        out.push(".section .rodata");
        strings.forEach((s, idx) => {
            out.push("str_" + idx + ":");
            out.push("\t.asciz \"" + escapeStr(s) + "\"");
        });
        out.push("");
    }

    // I/O helpers (Linux ARM syscalls)
    out.push(".section .text");
    out.push("");
    out.push("@ Freestanding I/O hooks — ARM Linux syscall ABI");
    out.push("@ io_putc: write one byte to stdout");
    out.push("io_putc:");
    out.push("\tpush {r0, r1, r2, r7, lr}");
    out.push("\tmov r0, #1");          // fd = stdout
    out.push("\tsub sp, sp, #4");
    out.push("\tstrb r3, [sp]");       // char byte on stack
    out.push("\tmov r1, sp");          // buf
    out.push("\tmov r2, #1");          // len = 1
    out.push("\tmov r7, #4");          // SYS_write = 4
    out.push("\tswi #0");
    out.push("\tadd sp, sp, #4");
    out.push("\tpop {r0, r1, r2, r7, pc}");
    out.push("");
    out.push("@ io_puts: write string (r0=ptr, r1=len)");
    out.push("io_puts:");
    out.push("\tpush {r0-r2, r7, lr}");
    out.push("\tmov r2, r1");          // len
    out.push("\tmov r1, r0");          // buf
    out.push("\tmov r0, #1");          // fd = stdout
    out.push("\tmov r7, #4");          // SYS_write
    out.push("\tswi #0");
    out.push("\tpop {r0-r2, r7, pc}");
    out.push("");

    // Functions
    for (const f of ast.funcs) {
        locals.clear();
        stack = 0;
        collectLocals(f).forEach(n => alloc(n));
        // Align stack to 8 bytes
        const frame = (stack + 7) & ~7;

        out.push(fname(f.name) + ":");
        out.push("\tpush {fp, lr}");
        out.push("\tmov fp, sp");
        out.push("\tsub sp, sp, #" + frame);

        // Load params from r0-r3
        for (let i = 0; i < f.params.length && i < 4; i++) {
            out.push("\tstr r" + i + ", [fp, #-" + alloc(f.params[i]) + "]");
        }

        stmts(f.body, out);

        // Default return 0
        out.push("\tmov r0, #0");
        out.push("\tmov sp, fp");
        out.push("\tpop {fp, pc}");
        out.push("");
    }

    // Entry point
    out.push(".globl _start");
    out.push("_start:");
    out.push("\tbl fn_main");
    out.push("\tmov r7, #1");     // SYS_exit
    out.push("\tswi #0");
    out.push("");

    return out.join("\n") + "\n";
}

/* ------------------------------------------------------------------ */
/* Helper functions (imported from mbjs-to-native.js logic)           */
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
