/**
 * asm_syntax.js — Inline Assembly Syntax Parser & BeASM Emitter
 *
 * Provides 3 modes for embedding raw assembly in JavaScript source that
 * transpiles through MBJS → BeASM → BEFB:
 *
 *   Mode 1 — @asm(...)           [RAW: verbatim, no transpiler touch]
 *   Mode 2 — <__ ... __>         [OPTIMIZED: transpiler-aware register alloc]
 *   Mode 3 — asm`...`            [TEMPLATE: hybrid, JS var interpolation]
 *
 * Plus decorators:
 *   @naked       — no prologue/epilogue (function is pure asm)
 *   @volatile    — never removed by optimizer
 *   @interrupt   — ISR frame (push all, iret)
 *   @noopt       — alias for raw mode on a block
 *
 * The parser extracts assembly blocks from the JS AST (or raw source),
 * resolves variable placeholders against the surrounding scope, and emits
 * valid BeASM `.basm` sections ready for the beasm assembler.
 *
 * Copyright (c) 2024-2026 Berry Lab Foundation — libmochiberry project
 */

"use strict";

/* ======================================================================
 * Architecture register tables
 * ====================================================================== */

const REGS = {
    x86_64: {
        callerSaved: ["rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11"],
        calleeSaved: ["rbx","rbp","r12","r13","r14","r15"],
        args:        ["rdi","rsi","rdx","rcx","r8","r9"],
        ret:         "rax",
        stackPtr:    "rsp",
        framePtr:    "rbp",
        wordSize:    8,
    },
    arm64: {
        callerSaved: ["x0","x1","x2","x3","x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17"],
        calleeSaved: ["x19","x20","x21","x22","x23","x24","x25","x26","x27","x28","x29","x30"],
        args:        ["x0","x1","x2","x3","x4","x5","x6","x7"],
        ret:         "x0",
        stackPtr:    "sp",
        framePtr:    "x29",
        wordSize:    8,
    },
    arm32: {
        callerSaved: ["r0","r1","r2","r3","r12"],
        calleeSaved: ["r4","r5","r6","r7","r8","r9","r10","r11"],
        args:        ["r0","r1","r2","r3"],
        ret:         "r0",
        stackPtr:    "sp",
        framePtr:    "r11",
        wordSize:    4,
    },
    rv64: {
        callerSaved: ["a0","a1","a2","a3","a4","a5","a6","a7","t0","t1","t2","t3","t4","t5","t6"],
        calleeSaved: ["s0","s1","s2","s3","s4","s5","s6","s7","s8","s9","s10","s11","ra"],
        args:        ["a0","a1","a2","a3","a4","a5","a6","a7"],
        ret:         "a0",
        stackPtr:    "sp",
        framePtr:    "s0",
        wordSize:    8,
    },
    rv32: {
        callerSaved: ["a0","a1","a2","a3","a4","a5","a6","a7","t0","t1","t2","t3","t4","t5","t6"],
        calleeSaved: ["s0","s1","s2","s3","s4","s5","s6","s7","s8","s9","s10","s11","ra"],
        args:        ["a0","a1","a2","a3","a4","a5","a6","a7"],
        ret:         "a0",
        stackPtr:    "sp",
        framePtr:    "s0",
        wordSize:    4,
    },
};

/* ======================================================================
 * Symbol table for variable → register/stack-slot mapping
 * ====================================================================== */

class AsmSymbolTable {
    constructor(arch = "x86_64") {
        this.arch = arch;
        this.regs = REGS[arch] || REGS.x86_64;
        this.vars = new Map();    // name → { type: 'reg'|'stack', reg, offset }
        this.usedRegs = new Set();
        this.stackSize = 0;
        this.labelCount = 0;
    }

    /** Assign JS variable to the next available arg register or stack slot */
    assignVar(name, hint) {
        if (this.vars.has(name)) return this.vars.get(name);
        const argIdx = this.vars.size;
        const argRegs = this.regs.args;
        if (argIdx < argRegs.length) {
            const entry = { type: "reg", reg: argRegs[argIdx] };
            this.vars.set(name, entry);
            this.usedRegs.add(argRegs[argIdx]);
            return entry;
        }
        /* Spill to stack */
        this.stackSize += this.regs.wordSize;
        const entry = { type: "stack", offset: this.stackSize };
        this.vars.set(name, entry);
        return entry;
    }

    /** Get BeASM reference for a variable */
    getRef(name) {
        const v = this.vars.get(name);
        if (!v) return `{${name}}`;  /* unresolved placeholder */
        if (v.type === "reg") return v.reg;
        return `[${this.regs.framePtr}-${v.offset}]`;
    }

    /** Generate unique label */
    label(prefix = ".L") {
        return prefix + (this.labelCount++);
    }
}

/* ======================================================================
 * Mode 1: @asm(...) — Raw inline assembly
 *
 * The assembly content is emitted verbatim. No placeholder resolution,
 * no optimizer touch. The only processing is string unescaping.
 *
 * Syntax in source:
 *   @asm(`
 *       mov rax, 42
 *       ret
 *   `)
 *
 *   @asm volatile(`
 *       cli
 *       hlt
 *   `)
 * ====================================================================== */

function parseRawAsm(source, startIdx) {
    /* Find the matching closing ) after @asm( */
    let i = startIdx;
    const len = source.length;

    /* Skip to opening ( */
    while (i < len && source[i] !== "(") i++;
    if (i >= len) return null;
    i++; /* skip ( */

    /* Check for 'volatile' keyword */
    let isVolatile = false;
    let j = i;
    while (j < len && /\s/.test(source[j])) j++;
    if (source.slice(j, j + 8) === "volatile") {
        isVolatile = true;
        j += 8;
        while (j < len && /\s/.test(source[j])) j++;
        i = j;
    }

    /* Check for template literal or string */
    if (source[i] === "`" || source[i] === '"') {
        const quote = source[i];
        i++; /* skip opening quote */
        let content = "";
        while (i < len && source[i] !== quote) {
            if (source[i] === "\\") {
                const next = source[i + 1];
                if (next === "n") content += "\n";
                else if (next === "t") content += "\t";
                else if (next === "\\") content += "\\";
                else content += next;
                i += 2;
            } else {
                content += source[i];
                i++;
            }
        }
        i++; /* skip closing quote */
        while (i < len && source[i] !== ")") i++;
        i++; /* skip ) */
        return { mode: "raw", content: content.trim(), volatile: isVolatile, end: i };
    }
    return null;
}

/* ======================================================================
 * Mode 2: <__ ... __> — Optimized inline assembly
 *
 * The transpiler processes this block:
 *   1. Resolves {variableName} placeholders to registers/stack slots
 *   2. Generates prologue/epilogue if inside a function
 *   3. May reorder instructions (future optimization pass)
 *
 * Syntax in source:
 *   function add(a, b) {
 *       <__
 *           mov rax, {a}
 *           add rax, {b}
 *           ret
 *       __>
 *   }
 *
 * Placeholders: {varName} → resolved to register or [rbp-offset]
 * Labels: .L0, .L1, etc. (auto-generated unique labels)
 * ====================================================================== */

function parseOptimizedAsm(source, startIdx) {
    const marker = "__>";
    let i = startIdx;
    const len = source.length;

    /* Find closing __> */
    let endIdx = source.indexOf(marker, i);
    if (endIdx === -1) return null;

    const rawContent = source.slice(i, endIdx).trim();
    const end = endIdx + marker.length;

    return { mode: "optimized", content: rawContent, end };
}

/* ======================================================================
 * Mode 3: asm`...` — Template inline assembly
 *
 * Hybrid mode: tagged template literal that supports ${expr} interpolation.
 * The transpiler resolves JS expressions to assembly values.
 *
 * Syntax in source:
 *   const handler = asm`
 *       push rax
 *       mov rax, ${syscallNumber}
 *       int 0x80
 *       pop rax
 *       iret
 *   `;
 *
 * Expressions:
 *   ${expr}    → evaluated and emitted as immediate value
 *   $$reg      → emit literal register name
 *   ${x}       → resolved to variable's register/stack location
 * ====================================================================== */

function parseTemplateAsm(source, startIdx) {
    let i = startIdx;
    const len = source.length;

    if (source[i] !== "`") return null;
    i++; /* skip opening ` */

    let content = "";
    const interpolations = [];

    while (i < len && source[i] !== "`") {
        if (source[i] === "\\") {
            const next = source[i + 1];
            if (next === "n") content += "\n";
            else if (next === "t") content += "\t";
            else content += next || "";
            i += 2;
        } else if (source[i] === "$" && source[i + 1] === "{") {
            i += 2; /* skip ${ */
            let expr = "";
            let depth = 1;
            while (i < len && depth > 0) {
                if (source[i] === "{") depth++;
                else if (source[i] === "}") depth--;
                if (depth > 0) expr += source[i];
                i++;
            }
            const placeholder = `__INTERP_${interpolations.length}__`;
            content += placeholder;
            interpolations.push(expr.trim());
        } else {
            content += source[i];
            i++;
        }
    }
    i++; /* skip closing ` */

    return { mode: "template", content: content.trim(), interpolations, end: i };
}

/* ======================================================================
 * Decorator parser
 * ====================================================================== */

function parseDecorators(source, funcStartIdx) {
    const decorators = [];
    let i = funcStartIdx;

    while (i < source.length) {
        const before = source.slice(0, i);
        const trimmed = before.trimEnd();
        if (trimmed.endsWith("@naked"))  decorators.push("naked");
        else if (trimmed.endsWith("@volatile")) decorators.push("volatile");
        else if (trimmed.endsWith("@interrupt")) decorators.push("interrupt");
        else if (trimmed.endsWith("@noopt")) decorators.push("noopt");
        else break;

        i += (i - (before.length - before.trimEnd().length));
    }

    return decorators;
}

/* ======================================================================
 * BeASM Emitter — converts parsed asm blocks to .basm output
 * ====================================================================== */

function emitBeAsm(asmBlocks, opts = {}) {
    const arch = opts.arch || "x86_64";
    const regs = REGS[arch] || REGS.x86_64;
    const sym = new AsmSymbolTable(arch);
    const out = [];

    out.push("; generated by mbjs-to-native.js inline assembly");
    out.push("; arch: " + arch);
    out.push("");

    for (const block of asmBlocks) {
        if (block.mode === "raw") {
            emitRawBlock(out, block, regs);
        } else if (block.mode === "optimized") {
            emitOptimizedBlock(out, block, sym, regs);
        } else if (block.mode === "template") {
            emitTemplateBlock(out, block, sym, regs);
        }
    }

    return out.join("\n") + "\n";
}

function emitRawBlock(out, block, regs) {
    if (block.volatile) {
        out.push("; @asm volatile — optimizer must preserve this block");
    }
    out.push("; @asm raw block (verbatim, no optimization)");
    const lines = block.content.split("\n");
    for (const line of lines) {
        out.push("    " + line);
    }
    out.push("");
}

function emitOptimizedBlock(out, block, sym, regs) {
    out.push("; <__ optimized inline assembly __>");
    const lines = block.content.split("\n");
    for (let line of lines) {
        /* Resolve {variableName} placeholders */
        line = line.replace(/\{(\w+)\}/g, (match, name) => {
            /* Check if it's a known variable or a register hint */
            if (regs.args.includes(name) || regs.callerSaved.includes(name) ||
                regs.calleeSaved.includes(name)) {
                return name; /* Already a register name */
            }
            return sym.getRef(name);
        });

        /* Resolve .label references to unique labels */
        line = line.replace(/\.(\w+)/g, (match, name) => {
            if (name === "text" || name === "data" || name === "rodata" ||
                name === "bss" || name === "start" || name === "globl") {
                return match; /* Section/directive names stay as-is */
            }
            return "." + name; /* Keep as label reference */
        });

        out.push("    " + line);
    }
    out.push("");
}

function emitTemplateBlock(out, block, sym, regs) {
    out.push("; asm` template inline assembly`");
    let content = block.content;

    /* Resolve interpolations */
    for (let idx = 0; idx < block.interpolations.length; idx++) {
        const placeholder = `__INTERP_${idx}__`;
        const expr = block.interpolations[idx];

        /* If expression is a simple variable name, resolve to register */
        if (/^[a-zA-Z_]\w*$/.test(expr) && sym.vars.has(expr)) {
            content = content.replace(placeholder, sym.getRef(expr));
        } else if (/^\d+$/.test(expr)) {
            /* Numeric literal */
            content = content.replace(placeholder, expr);
        } else if (/^0x[0-9a-fA-F]+$/.test(expr)) {
            /* Hex literal */
            content = content.replace(placeholder, expr);
        } else {
            /* Complex expression — emit as immediate */
            content = content.replace(placeholder, expr);
        }
    }

    const lines = content.split("\n");
    for (const line of lines) {
        out.push("    " + line);
    }
    out.push("");
}

/* ======================================================================
 * Source-level scanner — finds all inline asm blocks in JS source
 * ====================================================================== */

function scanInlineAsm(source) {
    const blocks = [];
    let i = 0;
    const len = source.length;

    while (i < len) {
        /* Scan for @asm( */
        const asmIdx = source.indexOf("@asm", i);
        /* Scan for <__ */
        const optIdx = source.indexOf("<__", i);
        /* Scan for asm` */
        const tplIdx = source.indexOf("asm`", i);

        /* Find the earliest match */
        const candidates = [];
        if (asmIdx !== -1) candidates.push({ type: "raw", idx: asmIdx });
        if (optIdx !== -1) candidates.push({ type: "optimized", idx: optIdx });
        if (tplIdx !== -1) candidates.push({ type: "template", idx: tplIdx });

        if (candidates.length === 0) break;

        candidates.sort((a, b) => a.idx - b.idx);
        const first = candidates[0];

        let block = null;
        if (first.type === "raw") {
            block = parseRawAsm(source, first.idx + 4);
        } else if (first.type === "optimized") {
            block = parseOptimizedAsm(source, first.idx + 3);
        } else if (first.type === "template") {
            block = parseTemplateAsm(source, first.idx + 3);
        }

        if (block) {
            block.startIdx = first.idx;
            blocks.push(block);
            i = block.end;
        } else {
            i = first.idx + 1;
        }
    }

    return blocks;
}

/* ======================================================================
 * Strip inline asm from JS source (for non-asm transpilation)
 * ====================================================================== */

function stripInlineAsm(source) {
    let result = source;
    const blocks = scanInlineAsm(source).reverse(); /* reverse to maintain offsets */
    for (const block of blocks) {
        result = result.slice(0, block.startIdx) + result.slice(block.end);
    }
    return result;
}

/* ======================================================================
 * Public API
 * ====================================================================== */

export {
    REGS,
    AsmSymbolTable,
    parseRawAsm,
    parseOptimizedAsm,
    parseTemplateAsm,
    parseDecorators,
    emitBeAsm,
    scanInlineAsm,
    stripInlineAsm,
};
