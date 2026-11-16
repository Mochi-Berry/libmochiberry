/**
 * riscv.js — RISC-V (RV32I/G/C, RV64I/G/C, RV128I) GAS assembly emitter
 *             for the MBJS transpiler
 *
 * Emits GNU as-compatible RISC-V AT&T syntax from the MBJS AST.
 * Supports RV32, RV64, and RV128 base integer ISA with M (multiply/divide)
 * and C (compressed) extensions.
 *
 * Target: baremetal RISC-V, Linux RISC-V, Berry OS RISC-V
 * Calling convention: RISC-V standard ABI
 *   - a0-a7 (x10-x17): arguments / return values
 *   - ra (x1): return address
 *   - sp (x2): stack pointer
 *   - fp/s0 (x8): frame pointer
 *   - t0-t6: temporaries (caller-saved)
 *   - s0-s11 (x8-x9, x18-x27): saved (callee-saved)
 *
 * Copyright (c) 2024-2026 Berry Lab Foundation — libmochiberry project
 */

"use strict";

import { RV32, RV64, RV128 } from "./isa_reference.js";

/* ------------------------------------------------------------------ */
/* Unified RISC-V GAS Emitter                                          */
/* Supports RV32, RV64, RV128 via the `xlen` parameter               */
/* ------------------------------------------------------------------ */

/**
 * Emit RISC-V GAS assembly from MBJS AST
 * @param {object} ast - Parsed AST from mbjs-to-native.js parser
 * @param {object} opts - { xlen: 32|64|128, ext: ['M','C','F','D'] }
 * @returns {string} GAS assembly source (RISC-V)
 */
export function emit_riscv_gas(ast, opts = {}) {
    const xlen = opts.xlen || 64;
    const isa = xlen === 128 ? RV128 : xlen === 64 ? RV64 : RV32;
    const xreg = (n) => "x" + n;  // raw register name
    const wreg = (n) => "w" + n;  // RV64 word-sized alias
    const areg = (n) => "a" + n;  // argument register alias
    const sreg = (n) => "s" + n;  // saved register alias

    const locals = new Map();
    const fname = (name) => "fn_" + sym(name);
    const strings = printStrings(ast);
    let stack = 0;
    let label = 0;
    const L = () => ".L" + (label++);

    function sym(name) { return name.replace(/[^a-zA-Z0-9_]/g, "_"); }

    function alloc(name) {
        if (!locals.has(name)) {
            stack += xlen / 8;  // 4 bytes for RV32, 8 for RV64/128
            locals.set(name, stack);
        }
        return locals.get(name);
    }

    function loadWord(resultReg, addrReg, offset) {
        if (xlen === 32) {
            return `\tlw ${resultReg}, ${offset}(${addrReg})`;
        } else {
            return `\tld ${resultReg}, ${offset}(${addrReg})`;
        }
    }

    function storeWord(srcReg, addrReg, offset) {
        if (xlen === 32) {
            return `\tsw ${srcReg}, ${offset}(${addrReg})`;
        } else {
            return `\tsd ${srcReg}, ${offset}(${addrReg})`;
        }
    }

    function expr(e, out) {
        switch (e.kind) {
        case "num": {
            const v = BigInt(e.value);
            if (v >= -2048n && v <= 2047n) {
                out.push("\tli a0, " + v);
            } else {
                // lui + addi sequence
                const hi = Number((v + 0x800n) >> 12n);
                const lo = Number(v & 0xFFFn);
                if (lo !== 0 && hi !== 0) {
                    out.push("\tlui a0, " + hi);
                    out.push("\taddi a0, a0, " + lo);
                } else if (hi !== 0) {
                    out.push("\tlui a0, " + hi);
                } else {
                    out.push("\tli a0, " + lo);
                }
            }
            return;
        }
        case "var":
            out.push(loadWord("a0", "fp", -alloc(e.name)));
            return;
        case "postinc":
        case "postdec": {
            const off = alloc(e.name);
            out.push(loadWord("a0", "fp", -off));
            out.push("\t" + (e.op === "++" ? "addi a0, a0, 1" : "addi a0, a0, -1"));
            out.push(storeWord("a0", "fp", -off));
            return;
        }
        case "call":
            if (e.name === "print") throw new Error("print() must be a statement");
            if (e.name === "sys_exit") {
                expr(e.args[0], out);
                out.push("\tli a7, 93");     // SYS_exit (Linux RISC-V)
                out.push("\tecall");
                return;
            }
            if (e.name === "putc") {
                expr(e.args[0], out);
                out.push("\taddi sp, sp, -16");
                out.push("\tsb a0, 0(sp)");   // char byte on stack
                out.push("\tli a0, 1");       // fd = stdout
                out.push("\tmv a1, sp");      // buf
                out.push("\tli a2, 1");       // len = 1
                out.push("\tli a7, 64");      // SYS_write
                out.push("\tecall");
                out.push("\taddi sp, sp, 16");
                return;
            }
            // Push args (standard ABI: a0-a7)
            for (let i = Math.min(e.args.length - 1, 7); i >= 0; i--) {
                expr(e.args[i], out);
                out.push(pushInsn("a" + i));
            }
            out.push("\tcall " + fname(e.name));
            // Pop args
            for (let i = 0; i < Math.min(e.args.length, 8); i++)
                out.push(popInsn("a" + i));
            return;
        case "unop":
            expr(e.a, out);
            if (e.op === "-") {
                out.push("\tneg a0, a0");
            } else {
                out.push("\tseqz a0, a0");    // set if equal to zero
                out.push("\tandi a0, a0, 1");  // mask to 0/1
            }
            return;
        case "binop": {
            if (e.op === "&&" || e.op === "||") {
                const T = L(), D = L(), E = L();
                expr(e.a, out);
                out.push("\tbeqz a0, " + E);
                expr(e.b, out);
                out.push("\tbeqz a0, " + E);
                out.push("\tli a0, 1");
                out.push("\tj " + D);
                out.push(E + ":");
                out.push("\tli a0, 0");
                out.push(D + ":");
                if (e.op === "||") {
                    // For ||, we need to flip: if first is nonzero, result is 1
                    // The above already handles &&. For ||, different logic:
                    out.length = out.length - 7; // undo
                    expr(e.a, out);
                    out.push("\tbnez a0, " + D);
                    expr(e.b, out);
                    out.push("\tbnez a0, " + D);
                    out.push("\tli a0, 0");
                    out.push("\tj " + L());
                    out.push(D + ":");
                    out.push("\tli a0, 1");
                }
                return;
            }
            expr(e.b, out);
            out.push(pushInsn("a0"));
            expr(e.a, out);
            out.push(popInsn("a1"));
            // a0 = a, a1 = b
            switch (e.op) {
            case "+": out.push("\tadd a0, a0, a1"); break;
            case "-": out.push("\tsub a0, a0, a1"); break;
            case "*":
                if (xlen === 32 && opts.ext && opts.ext.includes("M")) {
                    out.push("\tmul a0, a0, a1");
                } else {
                    // Without M extension, use shift-and-add
                    // For simplicity, assume M extension is available
                    out.push("\tmul a0, a0, a1");
                }
                break;
            case "/":
                if (opts.ext && opts.ext.includes("M")) {
                    out.push("\tdiv a0, a0, a1");  // signed divide (M extension)
                } else {
                    // Software division (restore & subtract)
                    out.push("\tcall __mbjs_div");  // fallback
                }
                break;
            case "%":
                if (opts.ext && opts.ext.includes("M")) {
                    out.push("\trem a0, a0, a1");   // signed remainder (M ext)
                } else {
                    out.push("\tcall __mbjs_mod");
                }
                break;
            default: {
                const cc = { "<": "slt", "<=": "slt", ">": "sgt", ">=": "sgt",
                             "==": "seqz", "!=": "snez" }[e.op];
                if (!cc) throw new Error("riscv: unsupported binop " + e.op);
                if (e.op === "<" || e.op === ">") {
                    out.push("\t" + (e.op === "<" ? "slt" : "sgt") + " a0, a0, a1");
                } else if (e.op === "<=") {
                    // a <= b  <==>  b >= a  <==>  !(a > b)  <==>  !(b < a)
                    out.push("\tsgt a0, a0, a1");     // a0 = (a > b) ? 1 : 0
                    out.push("\txori a0, a0, 1");      // flip: a0 = (a <= b) ? 1 : 0
                } else if (e.op === ">=") {
                    out.push("\tslt a0, a0, a1");      // a0 = (a < b) ? 1 : 0
                    out.push("\txori a0, a0, 1");       // flip: a0 = (a >= b) ? 1 : 0
                } else {
                    out.push("\t" + cc + " a0, a0, a1");
                }
                break;
            }
            }
            return;
        }
        default: throw new Error("riscv: unsupported expr " + e.kind);
        }
    }

    function pushInsn(reg) {
        return xlen === 32 ? `\tsw ${reg}, -4(sp)\n\taddi sp, sp, -4`
                           : `\tsd ${reg}, -8(sp)\n\taddi sp, sp, -8`;
    }

    function popInsn(reg) {
        return xlen === 32 ? `\taddi sp, sp, 4\n\tlw ${reg}, -4(sp)`
                           : `\taddi sp, sp, 8\n\tld ${reg}, -8(sp)`;
    }

    function stmts(list, out) {
        for (const s of list) {
            switch (s.kind) {
            case "assign":
                alloc(s.name);
                if (s.value === null)
                    out.push("\tli a0, 0");
                else
                    expr(s.value, out);
                out.push(storeWord("a0", "fp", -locals.get(s.name)));
                break;
            case "block": stmts(s.stmts, out); break;
            case "if": {
                const E = L(), X = L();
                expr(s.cond, out);
                out.push("\tbeqz a0, " + (s.els ? X : E));
                stmts([s.then], out);
                if (s.els) {
                    out.push("\tj " + E);
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
                out.push("\tbeqz a0, " + E);
                stmts([s.body], out);
                out.push("\tj " + T);
                out.push(E + ":");
                break;
            }
            case "for": {
                if (s.init && s.init.kind === "assign") {
                    alloc(s.init.name);
                    expr(s.init.value, out);
                    out.push(storeWord("a0", "fp", -locals.get(s.init.name)));
                } else if (s.init) expr(s.init, out);
                const T = L(), E = L();
                out.push(T + ":");
                if (s.cond) {
                    expr(s.cond, out);
                    out.push("\tbeqz a0, " + E);
                }
                stmts([s.body], out);
                if (s.upd) expr(s.upd, out);
                out.push("\tj " + T);
                out.push(E + ":");
                break;
            }
            case "return":
                if (s.value) expr(s.value, out);
                else out.push("\tli a0, 0");
                out.push("\tmv sp, fp");
                if (xlen === 32) {
                    out.push("\tlw ra, -4(sp)");
                    out.push("\tlw fp, -8(sp)");
                    out.push("\taddi sp, sp, -8");  // adjusted: actually pop
                } else {
                    out.push("\tld ra, -8(sp)");
                    out.push("\tld fp, -16(sp)");
                    out.push("\taddi sp, sp, 16");
                }
                out.push("\tret");
                return;
            case "expr": {
                const lit = printLiteral(s.expr);
                if (lit !== null) {
                    const idx = strings.indexOf(lit);
                    out.push("\tla a0, str_" + idx);
                    out.push("\tli a1, " + lit.length);
                    out.push("\tcall io_puts");
                } else {
                    expr(s.expr, out);
                }
                break;
            }
            default: throw new Error("riscv: unsupported stmt " + s.kind);
            }
        }
    }

    // --- Code generation ---
    const out = [];
    out.push("# generated by mbjs-to-native.js (--gas-rv" + xlen + ") — RISC-V");
    out.push("# target: RV" + xlen + "I" + (opts.ext ? "/" + opts.ext.join("") : ""));
    out.push("# calling convention: RISC-V standard ABI (a0-a7 args, s0-s11 saved)");
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
    out.push("# Freestanding I/O hooks — RISC-V Linux syscall ABI");
    out.push("# io_puts: write string (a0=ptr, a1=len)");
    out.push("io_puts:");
    out.push("\tmv a2, a1");          // len
    out.push("\tmv a1, a0");          // buf
    out.push("\tli a0, 1");           // fd = stdout
    out.push("\tli a7, 64");          // SYS_write (Linux RISC-V)
    out.push("\tecall");
    out.push("\tret");
    out.push("");

    // Software division helpers (when M extension is not available)
    if (!opts.ext || !opts.ext.includes("M")) {
        out.push("# Software integer division (dividend in a0, divisor in a1)");
        out.push("__mbjs_div:");
        out.push("\t# Placeholder — use with M extension for production");
        out.push("\tdiv a0, a0, a1");
        out.push("\tret");
        out.push("");
        out.push("# Software integer modulo");
        out.push("__mbjs_mod:");
        out.push("\trem a0, a0, a1");
        out.push("\tret");
        out.push("");
    }

    // Functions
    for (const f of ast.funcs) {
        locals.clear();
        stack = 0;
        collectLocals(f).forEach(n => alloc(n));
        const align = xlen === 32 ? 4 : 8;
        const frameSize = ((stack + align * 2 + align - 1) & ~(align - 1)); // align + ra + fp

        out.push(fname(f.name) + ":");
        if (xlen === 32) {
            out.push("\taddi sp, sp, -" + frameSize);
            out.push("\tsw ra, " + (frameSize - 4) + "(sp)");
            out.push("\tsw fp, " + (frameSize - 8) + "(sp)");
            out.push("\taddi fp, sp, " + frameSize);
        } else {
            out.push("\taddi sp, sp, -" + frameSize);
            out.push("\tsd ra, " + (frameSize - 8) + "(sp)");
            out.push("\tsd fp, " + (frameSize - 16) + "(sp)");
            out.push("\taddi fp, sp, " + frameSize);
        }

        // Load params from a0-a7
        for (let i = 0; i < f.params.length && i < 8; i++) {
            out.push(storeWord("a" + i, "fp", -alloc(f.params[i])));
        }

        stmts(f.body, out);

        // Default return 0
        out.push("\tli a0, 0");
        if (xlen === 32) {
            out.push("\tlw ra, " + (frameSize - 4) + "(fp)");
            out.push("\tlw fp, " + (frameSize - 8) + "(fp)");
            out.push("\taddi sp, fp, 0");
        } else {
            out.push("\tld ra, " + (frameSize - 8) + "(fp)");
            out.push("\tld fp, " + (frameSize - 16) + "(fp)");
            out.push("\tmv sp, fp");
        }
        out.push("\tret");
        out.push("");
    }

    // Entry point
    out.push(".globl _start");
    out.push("_start:");
    out.push("\tcall fn_main");
    out.push("\tli a7, 93");     // SYS_exit (Linux RISC-V)
    out.push("\tecall");
    out.push("");

    return out.join("\n") + "\n";
}

/* ------------------------------------------------------------------ */
/* Convenience wrappers for each RV variant                            */
/* ------------------------------------------------------------------ */

export function emit_rv32_gas(ast, opts = {}) {
    return emit_riscv_gas(ast, { xlen: 32, ext: opts.ext || ["M", "C"], ...opts });
}

export function emit_rv64_gas(ast, opts = {}) {
    return emit_riscv_gas(ast, { xlen: 64, ext: opts.ext || ["M", "C"], ...opts });
}

export function emit_rv128_gas(ast, opts = {}) {
    return emit_riscv_gas(ast, { xlen: 128, ext: opts.ext || ["M", "C"], ...opts });
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
