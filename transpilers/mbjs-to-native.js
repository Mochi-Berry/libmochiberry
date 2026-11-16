/*
 * mbjs-to-native.js — transpile a subset of JavaScript to freestanding
 * native code, the AOT path for Berry OS / bare-metal.
 *
 *                 +-----------------------+   --c    ->  gcc / clang
 *                 |                       |   --c3   ->  c3c (C3 + asm blocks)
 *                 |   JS subset parser    |   --gas  ->  GNU as (x86-64 .s)
 *                 |                       |   --llvm ->  llvm-as (.ll)
 *                 +-----------------------+   --basm ->  beasm (.basm ->
 *                                                        Berry .bex/.befb)
 *
 * Supported subset (deliberately small, documented in transpilers/README.md):
 *   - 64-bit integers only; + - * / %  < <= > >= == !=  && || !  ( )
 *   - let / const / var declarations, if/else, while, for(;;)
 *   - function definitions + calls (recursion ok), return
 *   - builtins: print("...")  putc(expr)  sys_exit(expr)
 * No objects, closures, arrays, or strings outside print literals.
 *
 * Usage (run in module mode so the std module is available):
 *   build/mbjs -m transpilers/mbjs-to-native.js app.js --c -o out.c
 *   build/mbjs -m transpilers/mbjs-to-native.js app.js --basm -o out.basm
 *
 * Written in pure JS; runs under build/mbjs (-m) or node --experimental-vm-modules.
 */

import * as std from "mbjs:std";

"use strict";

/* ------------------------------------------------------------------ */
/* Command line                                                        */
/* ------------------------------------------------------------------ */

function js_usage() {
    print("usage: mbjs-to-native.js <app.js> --c|--c3|--gas|--gas-arm32|--gas-arm64|--gas-rv32|--gas-rv64|--gas-rv128|--llvm|--basm [-o out]");
    std.exit(1);
}

/* scriptArgs[0] is this transpiler's own path (engine convention).
   A leading "--" separator (mbjs -m mod -- args) is skipped. */
let js_args = scriptArgs.slice(1);
if (js_args[0] === "--")
    js_args = js_args.slice(1);
if (js_args.length < 2)
    js_usage();

/* --target flag: auto-detect compilation mode from pragmas */
let js_target_mode = null;
for (let i = 0; i < js_args.length; i++) {
    if (js_args[i] === "--target" && i + 1 < js_args.length) {
        js_target_mode = js_args[++i];
    }
}

const js_src_file = js_args[0];
let js_mode = js_args[1];
/* If --target flag is used, read pragmas from source to determine mode */
if (js_target_mode) {
    const js_src_pragma = std.loadFile(js_src_file);
    if (js_src_pragma) {
        const archMatch = js_src_pragma.match(/\/\/\s*@arch\s+(x86_64|arm64|arm32|riscv32|riscv64|riscv128)/);
        const targetMatch = js_src_pragma.match(/\/\/\s*@target\s+(host|guest)/);
        const pipelineMatch = js_src_pragma.match(/\/\/\s*@pipeline\s+(interpreter|jit|aot|befb)/);

        // Priority: explicit @pipeline > @target guest → --basm > @arch → specific GAS flag
        if (pipelineMatch) {
            if (pipelineMatch[1] === "befb") js_mode = "--basm";
            else if (pipelineMatch[1] === "aot") js_mode = "--c";
            else if (pipelineMatch[1] === "jit") js_mode = "--c";
        } else if (targetMatch && targetMatch[1] === "guest") {
            // guest always defaults to --basm (BEFB) unless @arch overrides
            js_mode = "--basm";
        } else if (archMatch) {
            const archMap = {
                "x86_64": "--gas", "x86_32": "--gas", "x86_16": "--gas",
                "arm64": "--gas-arm64", "arm32": "--gas-arm32",
                "riscv32": "--gas-rv32", "riscv64": "--gas-rv64", "riscv128": "--gas-rv128"
            };
            js_mode = archMap[archMatch[1]] || "--gas";
        } else if (targetMatch && targetMatch[1] === "host") {
            js_mode = "--c";
        }
        print("[target_select] Auto-detected mode: " + js_mode);
    }
}

if (!["--c", "--c3", "--gas", "--gas-arm32", "--gas-arm64", "--gas-rv32", "--gas-rv64", "--gas-rv128", "--llvm", "--basm"].includes(js_mode))
    js_usage();
let js_out_file = null;
for (let i = 2; i < js_args.length; i++) {
    if (js_args[i] === "-o" && i + 1 < js_args.length)
        js_out_file = js_args[++i];
}

/* ------------------------------------------------------------------ */
/* Tokenizer                                                           */
/* ------------------------------------------------------------------ */

function js_tokenize(src) {
    const tokens = [];
    let i = 0;
    const n = src.length;
    while (i < n) {
        const c = src[i];
        if (c === " " || c === "\t" || c === "\n" || c === "\r") { i++; continue; }
        if (c === "/" && src[i + 1] === "/") {
            while (i < n && src[i] !== "\n") i++;
            continue;
        }
        if (c === "/" && src[i + 1] === "*") {
            i += 2;
            while (i < n && !(src[i] === "*" && src[i + 1] === "/")) i++;
            i += 2;
            continue;
        }
        if (c === '"') {
            let s = "";
            i++;
            while (i < n && src[i] !== '"') {
                if (src[i] === "\\" && i + 1 < n) {
                    const e = src[i + 1];
                    if (e === "n") s += "\n";
                    else if (e === "t") s += "\t";
                    else if (e === "r") s += "\r";
                    else if (e === "\\") s += "\\";
                    else if (e === '"') s += '"';
                    else s += e;
                    i += 2;
                } else {
                    s += src[i];
                    i++;
                }
            }
            i++;
            tokens.push({ type: "str", value: s });
            continue;
        }
        if (/[0-9]/.test(c) || (c === "-" && /[0-9]/.test(src[i + 1] || ""))) {
            let num = "";
            if (c === "-") { num = "-"; i++; }
            while (i < n && /[0-9a-fA-FxX_]/.test(src[i])) num += src[i++];
            tokens.push({ type: "num", value: num.replace(/_/g, "") });
            continue;
        }
        if (/[a-zA-Z_$]/.test(c)) {
            let id = "";
            while (i < n && /[a-zA-Z0-9_$]/.test(src[i])) id += src[i++];
            tokens.push({ type: "id", value: id });
            continue;
        }
        const two = src.substr(i, 2);
        if (["==", "!=", "<=", ">=", "&&", "||", "++", "--"].includes(two)) {
            tokens.push({ type: "op", value: two });
            i += 2;
            continue;
        }
        if ("+-*/%<>!(){};,=".includes(c)) {
            tokens.push({ type: "op", value: c });
            i++;
            continue;
        }
        throw new Error("unexpected char '" + c + "' at offset " + i);
    }
    tokens.push({ type: "eof", value: "" });
    return tokens;
}

/* ------------------------------------------------------------------ */
/* Parser (recursive descent)                                          */
/* ------------------------------------------------------------------ */

function js_parse(tokens) {
    let pos = 0;
    const peek = () => tokens[pos];
    const next = () => tokens[pos++];
    const expect = (v) => {
        const t = next();
        const bad = v === "id" ? t.type !== "id" : t.value !== v;
        if (bad)
            throw new Error("expected '" + v + "' got '" + t.value + "'");
        return t;
    };

    function parse_program() {
        const funcs = [];
        while (peek().type !== "eof") {
            if (peek().value === "function")
                funcs.push(parse_funcdef());
            else
                throw new Error("top-level: expected 'function'");
        }
        return { kind: "program", funcs };
    }

    function parse_funcdef() {
        next();
        const name = expect("id").value;
        expect("(");
        const params = [];
        while (peek().value !== ")") {
            params.push(next().value);
            if (peek().value === ",") next();
        }
        expect(")");
        return { kind: "func", name, params, body: parse_block() };
    }

    function parse_block() {
        expect("{");
        const stmts = [];
        while (peek().value !== "}") {
            if (peek().type === "eof")
                throw new Error("unexpected EOF in block");
            stmts.push(parse_stmt());
        }
        expect("}");
        return stmts;
    }

    function parse_stmt() {
        const t = peek();
        if (t.value === "{")
            return { kind: "block", stmts: parse_block() };
        if (t.value === "if") {
            next();
            expect("(");
            const cond = parse_expr();
            expect(")");
            const then = parse_stmt();
            let els = null;
            if (peek().value === "else") {
                next();
                els = parse_stmt();
            }
            return { kind: "if", cond, then, els };
        }
        if (t.value === "while") {
            next();
            expect("(");
            const cond = parse_expr();
            expect(")");
            return { kind: "while", cond, body: parse_stmt() };
        }
        if (t.value === "for") {
            next();
            expect("(");
            let init = null;
            if (peek().value !== ";") {
                if (["let", "const", "var"].includes(peek().value)) {
                    next();
                    const name = expect("id").value;
                    expect("=");
                    init = { kind: "assign", name, value: parse_expr(), decl: true };
                } else {
                    init = parse_expr();
                }
            }
            expect(";");
            let cond = null;
            if (peek().value !== ";")
                cond = parse_expr();
            expect(";");
            let upd = null;
            if (peek().value !== ")")
                upd = parse_expr();
            expect(")");
            return { kind: "for", init, cond, upd, body: parse_stmt() };
        }
        if (["let", "const", "var"].includes(t.value)) {
            next();
            const name = expect("id").value;
            let value = null;
            if (peek().value === "=") {
                next();
                value = parse_expr();
            }
            expect(";");
            return { kind: "assign", name, value, decl: true };
        }
        if (t.value === "return") {
            next();
            let value = null;
            if (peek().value !== ";")
                value = parse_expr();
            expect(";");
            return { kind: "return", value };
        }
        /* plain assignment: id = expr; (re-assignment of an existing var) */
        if (t.type === "id" && tokens[pos + 1] && tokens[pos + 1].value === "=") {
            next();
            next();
            const value = parse_expr();
            expect(";");
            return { kind: "assign", name: t.value, value, decl: false };
        }
        /* Handle bare semicolons (empty statements from stripped asm blocks) */
        if (t.value === ";") {
            next();
            return { kind: "block", stmts: [] };
        }
        const e = parse_expr();
        expect(";");
        return { kind: "expr", expr: e };
    }

    function parse_expr() { return parse_logical_or(); }
    function parse_logical_or() {
        let e = parse_logical_and();
        while (peek().value === "||") {
            next();
            e = { kind: "binop", op: "||", a: e, b: parse_logical_and() };
        }
        return e;
    }
    function parse_logical_and() {
        let e = parse_equality();
        while (peek().value === "&&") {
            next();
            e = { kind: "binop", op: "&&", a: e, b: parse_equality() };
        }
        return e;
    }
    function parse_equality() {
        let e = parse_relational();
        while (peek().value === "==" || peek().value === "!=") {
            const op = next().value;
            e = { kind: "binop", op, a: e, b: parse_relational() };
        }
        return e;
    }
    function parse_relational() {
        let e = parse_additive();
        while (["<", "<=", ">", ">="].includes(peek().value)) {
            const op = next().value;
            e = { kind: "binop", op, a: e, b: parse_additive() };
        }
        return e;
    }
    function parse_additive() {
        let e = parse_multiplicative();
        while (peek().value === "+" || peek().value === "-") {
            const op = next().value;
            e = { kind: "binop", op, a: e, b: parse_multiplicative() };
        }
        return e;
    }
    function parse_multiplicative() {
        let e = parse_unary();
        while (["*", "/", "%"].includes(peek().value)) {
            const op = next().value;
            e = { kind: "binop", op, a: e, b: parse_unary() };
        }
        return e;
    }
    function parse_unary() {
        const t = peek();
        if (t.value === "!" || t.value === "-") {
            next();
            return { kind: "unop", op: t.value, a: parse_unary() };
        }
        return parse_primary();
    }
    function parse_primary() {
        const t = next();
        if (t.type === "num")
            return { kind: "num", value: BigInt(t.value) };
        if (t.type === "str")
            return { kind: "str", value: t.value };
        if (t.type === "id") {
            if (t.value === "true") return { kind: "num", value: 1n };
            if (t.value === "false") return { kind: "num", value: 0n };
            if (peek().value === "(") {
                next();
                const args = [];
                while (peek().value !== ")") {
                    args.push(parse_expr());
                    if (peek().value === ",") next();
                }
                expect(")");
                return { kind: "call", name: t.value, args };
            }
            if (peek().value === "++" || peek().value === "--") {
                const op = next().value;
                return { kind: "postinc", op, name: t.value };
            }
            return { kind: "var", name: t.value };
        }
        if (t.value === "(") {
            const e = parse_expr();
            expect(")");
            return e;
        }
        throw new Error("unexpected token '" + t.value + "'");
    }

    const ast = parse_program();
    if (peek().type !== "eof")
        throw new Error("trailing tokens after program");
    return ast;
}

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

let js_uid = 0;
const js_mangle = (name) => "jsv_" + name + "_" + (js_uid++);
const js_sym = (name) => name.replace(/[^a-zA-Z0-9_]/g, "_");

/* collect every string literal used as a print() argument, in order */
function js_print_strings(ast) {
    const out = [];
    (function walk(node) {
        if (node && typeof node === "object") {
            if (node.kind === "call" && node.name === "print" &&
                node.args.length === 1 && node.args[0].kind === "str")
                out.push(node.args[0].value);
            for (const k of Object.keys(node)) {
                const v = node[k];
                if (Array.isArray(v)) for (const x of v) walk(x);
                else walk(v);
            }
        }
    })(ast);
    return [...new Set(out)];
}

/* assert an expression is a plain print("...") statement */
function js_print_literal(e) {
    if (e.kind === "call" && e.name === "print" &&
        e.args.length === 1 && e.args[0].kind === "str")
        return e.args[0].value;
    return null;
}

/* collect every variable a function references, in first-use order
 * (params first), so the stack-frame emitters can reserve room for all
 * locals BEFORE any push/pop spilling happens. Without this, pushes at
 * rsp-8 collide with locals stored at rbp-8. */
function js_collect_locals(func) {
    const names = [];
    const seen = new Set();
    const add = (n) => { if (!seen.has(n)) { seen.add(n); names.push(n); } };
    for (const p of func.params) add(p);
    (function walk(node) {
        if (!node || typeof node !== "object") return;
        if (node.kind === "assign" || node.kind === "postinc" ||
            node.kind === "postdec" || node.kind === "var")
            add(node.name);
        for (const k of Object.keys(node)) {
            const v = node[k];
            if (Array.isArray(v)) { for (const x of v) walk(x); }
            else walk(v);
        }
    })(func.body);
    return names;
}

/* ------------------------------------------------------------------ */
/* --c mode: freestanding C99 with inline __asm__ (gcc / clang)        */
/* ------------------------------------------------------------------ */

function js_emit_c(ast) {
    const locals = new Map();
    const fname = (name) => "fn_" + js_sym(name);
    const strings = js_print_strings(ast);

    function expr(e) {
        switch (e.kind) {
        case "num": return e.value.toString();
        case "var": {
            if (!locals.has(e.name))
                throw new Error("undefined variable '" + e.name + "'");
            return locals.get(e.name);
        }
        case "call":
            if (e.name === "print")
                throw new Error("print() must be a statement");
            if (e.name === "putc")
                return "io_putc(" + expr(e.args[0]) + ")";
            if (e.name === "sys_exit")
                return "io_exit(" + expr(e.args[0]) + ")";
            return fname(e.name) + "(" +
                e.args.map((a) => expr(a)).join(", ") + ")";
        case "postinc":
        case "postdec": {
            if (!locals.has(e.name))
                throw new Error("undefined variable '" + e.name + "'");
            return locals.get(e.name) + (e.op === "++" ? "++" : "--");
        }
        case "unop":
            return e.op === "-" ? "(-(" + expr(e.a) + "))" : "(!(" + expr(e.a) + "))";
        case "binop":
            if (e.op === "&&") return "((" + expr(e.a) + ") && (" + expr(e.b) + "))";
            if (e.op === "||") return "((" + expr(e.a) + ") || (" + expr(e.b) + "))";
            return "((" + expr(e.a) + ") " + e.op + " (" + expr(e.b) + "))";
        default:
            throw new Error("c: unsupported expr " + e.kind);
        }
    }
    function mklocal(name) {
        if (!locals.has(name)) locals.set(name, js_mangle(name));
        return locals.get(name);
    }

    function stmts(list, out) {
        for (const s of list) {
            switch (s.kind) {
            case "assign":
                if (s.value === null)
                    out.push("    long long " + mklocal(s.name) + " = 0;");
                else if (s.decl === false)
                    out.push("    " + locals.get(s.name) + " = " + expr(s.value) + ";");
                else
                    out.push("    long long " + mklocal(s.name) + " = " + expr(s.value) + ";");
                break;
            case "block":
                out.push("    {");
                stmts(s.stmts, out);
                out.push("    }");
                break;
            case "if":
                out.push("    if (" + expr(s.cond) + ") {");
                stmts([s.then], out);
                if (s.els) {
                    out.push("    } else {");
                    stmts([s.els], out);
                }
                out.push("    }");
                break;
            case "while":
                out.push("    while (" + expr(s.cond) + ") {");
                stmts([s.body], out);
                out.push("    }");
                break;
            case "for":
                if (s.init && s.init.kind === "assign" && s.init.decl !== false)
                    out.push("    long long " + mklocal(s.init.name) + " = " +
                        expr(s.init.value) + ";");
                else if (s.init && s.init.kind === "assign")
                    out.push("    " + locals.get(s.init.name) + " = " +
                        expr(s.init.value) + ";");
                else if (s.init) expr(s.init);
                out.push("    for (; " + (s.cond ? expr(s.cond) : "1") + "; " +
                    (s.upd ? expr(s.upd) : "") + ") {");
                stmts([s.body], out);
                out.push("    }");
                break;
            case "return":
                out.push("    return " + (s.value ? expr(s.value) : "0") + ";");
                break;
            case "expr": {
                const lit = js_print_literal(s.expr);
                if (lit !== null) {
                    const idx = strings.indexOf(lit);
                    out.push("    { const char *p = js_str_" + idx +
                        "; while (*p) io_putc(*p++); }");
                } else {
                    out.push("    " + expr(s.expr) + ";");
                }
                break;
            }
            default:
                throw new Error("c: unsupported stmt " + s.kind);
            }
        }
    }

    const out = [];
    out.push("/* generated by mbjs-to-native.js (--c mode)");
    out.push(" * freestanding C99 for gcc/clang: no libc, inline __asm__ hooks. */");
    out.push("typedef long long i64;");
    out.push("");
    out.push("/* freestanding inline-assembly I/O hooks (x86-64 Linux syscall ABI;");
    out.push(" * remap for Berry OS: SYS_BERRY_SOW = 0x02, SYS_BERRY_WITHER = 0x05) */");
    out.push("static void io_putc(char c) {");
    out.push("    __asm__ volatile (");
    out.push("        \"movq $1, %%rax\\n\\t\"   /* SYS_write */");
    out.push("        \"movq $1, %%rdi\\n\\t\"   /* fd = stdout */");
    out.push("        \"leaq %0, %%rsi\\n\\t\"");
    out.push("        \"movq $1, %%rdx\\n\\t\"");
    out.push("        \"syscall\"");
    out.push("        : : \"m\"(c) : \"rax\", \"rdi\", \"rsi\", \"rdx\", \"rcx\", \"r11\", \"memory\");");
    out.push("}");
    out.push("static void io_exit(int code) {");
    out.push("    __asm__ volatile (");
    out.push("        \"movq $60, %%rax\\n\\t\"  /* SYS_exit */");
    out.push("        \"syscall\"");
    out.push("        : : \"D\"((long)code) : \"rax\", \"rcx\", \"r11\", \"memory\");");
    out.push("}");
    out.push("");
    strings.forEach((s, idx) => {
        let bytes = "";
        for (const ch of s)
            bytes += ch.codePointAt(0) < 128 ? ch : "?";
        out.push("static const char js_str_" + idx + "[] = \"" +
            bytes.replace(/\\/g, "\\\\").replace(/"/g, '\\"')
                 .replace(/\n/g, "\\n").replace(/\t/g, "\\t") + "\";");
    });
    /* forward declarations so recursive / mutually-called functions
     * compile cleanly before their definitions are seen */
    if (strings.length) out.push("");
    for (const f of ast.funcs) {
        out.push("static i64 " + fname(f.name) + "(" +
            f.params.map(() => "i64").join(", ") + ");");
    }
    out.push("");
    for (const f of ast.funcs) {
        locals.clear();
        f.params.forEach((p) => mklocal(p));
        out.push("static i64 " + fname(f.name) + "(" +
            f.params.map((p) => "i64 " + locals.get(p)).join(", ") + ") {");
        stmts(f.body, out);
        out.push("}");
        out.push("");
    }
    out.push("int main(void) {");
    out.push("    return (int)fn_main();");
    out.push("}");
    return out.join("\n") + "\n";
}

/* ------------------------------------------------------------------ */
/* --gas mode: GNU as (x86-64 AT&T)                                    */
/* ------------------------------------------------------------------ */

function js_emit_gas(ast) {
    const locals = new Map();
    const fname = (name) => "fn_" + js_sym(name);
    const strings = js_print_strings(ast);
    let js_stack = 0;
    let js_label = 0;
    const jl = () => ".L" + (js_label++);

    function alloc(name) {
        if (!locals.has(name)) {
            js_stack += 8;
            locals.set(name, js_stack);
        }
        return locals.get(name);
    }

    function expr(e, out) {
        switch (e.kind) {
        case "num": out.push("\tmovq $" + e.value + ", %rax"); return;
        case "var": out.push("\tmovq -" + alloc(e.name) + "(%rbp), %rax"); return;
        case "postinc":
        case "postdec":
            out.push("\t" + (e.op === "++" ? "incq" : "decq") + " -" +
                alloc(e.name) + "(%rbp)");
            return;
        case "call":
            if (e.name === "print")
                throw new Error("print() must be a statement");
            if (e.name === "sys_exit") {
                expr(e.args[0], out);
                out.push("\tmovq %rax, %rdi");
                out.push("\tmovq $60, %rax");   /* SYS_exit */
                out.push("\tsyscall");
                return;
            }
            if (e.name === "putc") {
                expr(e.args[0], out);
                out.push("\tpushq %rax");
                out.push("\tmovq $1, %rdi");
                out.push("\tmovq %rsp, %rsi");
                out.push("\tmovq $1, %rdx");
                out.push("\tmovq $1, %rax");    /* SYS_write */
                out.push("\tsyscall");
                out.push("\tpopq %rax");
                return;
            }
            for (let i = e.args.length - 1; i >= 0; i--) {
                expr(e.args[i], out);
                out.push("\tpushq %rax");
            }
            {
                const regs = ["%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"];
                for (let i = 0; i < Math.min(e.args.length, 6); i++)
                    out.push("\tpopq " + regs[i]);
            }
            out.push("\tcall " + fname(e.name));
            return;
        case "unop":
            if (e.op === "-") { expr(e.a, out); out.push("\tnegq %rax"); return; }
            expr(e.a, out);
            out.push("\ttestq %rax, %rax");
            out.push("\tsete %al");
            out.push("\tmovzbl %al, %eax");
            return;
        case "binop": {
            if (e.op === "&&" || e.op === "||") {
                const T = jl(), E = jl(), D = jl();
                expr(e.a, out);
                out.push("\ttestq %rax, %rax");
                out.push(e.op === "&&" ? "\tjz " + E : "\tjnz " + T);
                expr(e.b, out);
                out.push("\ttestq %rax, %rax");
                out.push(e.op === "&&" ? "\tjz " + E : "\tjnz " + T);
                out.push("\tmovq $1, %rax");
                out.push("\tjmp " + D);
                out.push(E + ":");
                out.push("\txorq %rax, %rax");
                out.push(D + ":");
                return;
            }
            expr(e.b, out);
            out.push("\tpushq %rax");
            expr(e.a, out);
            out.push("\tpopq %rcx");
            switch (e.op) {
            case "+": out.push("\taddq %rcx, %rax"); break;
            case "-": out.push("\tsubq %rcx, %rax"); break;
            case "*": out.push("\timulq %rcx, %rax"); break;
            case "/": out.push("\tcqto"); out.push("\tidivq %rcx"); break;
            case "%": out.push("\tcqto"); out.push("\tidivq %rcx");
                      out.push("\tmovq %rdx, %rax"); break;
            default: {
                const cc = { "<": "setl", "<=": "setle", ">": "setg",
                             ">=": "setge", "==": "sete", "!=": "setne" }[e.op];
                if (!cc) throw new Error("gas: unsupported binop " + e.op);
                out.push("\tcmpq %rcx, %rax");
                out.push("\t" + cc + " %al");
                out.push("\tmovzbl %al, %eax");
                break;
            }
            }
            return;
        }
        default: throw new Error("gas: unsupported expr " + e.kind);
        }
    }

    function stmts(list, out) {
        for (const s of list) {
            switch (s.kind) {
            case "assign":
                alloc(s.name);
                if (s.value === null)
                    out.push("\tmovq $0, -" + locals.get(s.name) + "(%rbp)");
                else {
                    expr(s.value, out);
                    out.push("\tmovq %rax, -" + locals.get(s.name) + "(%rbp)");
                }
                break;
            case "block": stmts(s.stmts, out); break;
            case "if": {
                expr(s.cond, out);
                const E = jl();
                out.push("\ttestq %rax, %rax");
                out.push("\tjz " + E);
                stmts([s.then], out);
                if (s.els) {
                    const X = jl();
                    out.push("\tjmp " + X);
                    out.push(E + ":");
                    stmts([s.els], out);
                    out.push(X + ":");
                } else out.push(E + ":");
                break;
            }
            case "while": {
                const T = jl(), E = jl();
                out.push(T + ":");
                expr(s.cond, out);
                out.push("\ttestq %rax, %rax");
                out.push("\tjz " + E);
                stmts([s.body], out);
                out.push("\tjmp " + T);
                out.push(E + ":");
                break;
            }
            case "for": {
                if (s.init && s.init.kind === "assign") {
                    alloc(s.init.name);
                    expr(s.init.value, out);
                    out.push("\tmovq %rax, -" + locals.get(s.init.name) + "(%rbp)");
                } else if (s.init) expr(s.init, out);
                const T = jl(), E = jl();
                out.push(T + ":");
                if (s.cond) {
                    expr(s.cond, out);
                    out.push("\ttestq %rax, %rax");
                    out.push("\tjz " + E);
                }
                stmts([s.body], out);
                if (s.upd) expr(s.upd, out);
                out.push("\tjmp " + T);
                out.push(E + ":");
                break;
            }
            case "return":
                if (s.value) expr(s.value, out);
                else out.push("\txorq %rax, %rax");
                out.push("\tleave");
                out.push("\tret");
                return;
            case "expr": {
                const lit = js_print_literal(s.expr);
                if (lit !== null) {
                    const idx = strings.indexOf(lit);
                    out.push("\tleaq js_msg_" + idx + "(%rip), %rsi");
                    out.push("\tmovq $" + lit.length + ", %rdx");
                    out.push("\tmovq $1, %rdi");
                    out.push("\tmovq $1, %rax");   /* SYS_write */
                    out.push("\tsyscall");
                } else {
                    expr(s.expr, out);
                }
                break;
            }
            default: throw new Error("gas: unsupported stmt " + s.kind);
            }
        }
    }

    const out = [];
    out.push("# generated by mbjs-to-native.js (--gas mode) — x86-64 AT&T");
    if (strings.length) {
        out.push(".section .rodata");
        strings.forEach((s, idx) => {
            const esc = s.replace(/\\/g, "\\\\").replace(/"/g, '\\"')
                         .replace(/\n/g, "\\n").replace(/\t/g, "\\t");
            out.push("js_msg_" + idx + ":");
            out.push("\t.string \"" + esc + "\"");
        });
        out.push(".section .text");
    }
    out.push(".globl _start");
    out.push("_start:");
    out.push("\tcall fn_main");
    out.push("\tmovq %rax, %rdi");
    out.push("\tmovq $60, %rax");
    out.push("\tsyscall");
    out.push("");
    for (const f of ast.funcs) {
        locals.clear();
        js_stack = 0;
        /* pre-scan: reserve stack for every local before any push/pop */
        js_collect_locals(f).forEach((n) => alloc(n));
        const frame = (js_stack + 15) & ~15;
        out.push(fname(f.name) + ":");
        out.push("\tpushq %rbp");
        out.push("\tmovq %rsp, %rbp");
        if (frame > 0)
            out.push("\tsubq $" + frame + ", %rsp");
        for (let i = 0; i < f.params.length; i++)
            out.push("\tmovq " + ["%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"][i] +
                ", -" + alloc(f.params[i]) + "(%rbp)");
        stmts(f.body, out);
        out.push("\txorq %rax, %rax");
        out.push("\tleave");
        out.push("\tret");
        out.push("");
    }
    return out.join("\n") + "\n";
}

/* ------------------------------------------------------------------ */
/* --llvm mode: LLVM IR                                                */
/* ------------------------------------------------------------------ */

function js_emit_llvm(ast) {
    const locals = new Map();
    const fname = (name) => "fn_" + js_sym(name);
    const strings = js_print_strings(ast);
    let js_tmp = 0;
    const tmp = () => "%t" + (js_tmp++);

    function expr(e, out) {
        const r = tmp();
        switch (e.kind) {
        case "num":
            out.push("  " + r + " = add i64 0, " + e.value);
            return r;
        case "var": {
            const l = locals.get(e.name);
            if (!l) throw new Error("llvm: undefined var " + e.name);
            out.push("  " + r + " = load i64, i64* " + l);
            return r;
        }
        case "postinc":
        case "postdec": {
            const l = locals.get(e.name);
            if (!l) throw new Error("llvm: undefined var " + e.name);
            const v = tmp();
            out.push("  " + v + " = load i64, i64* " + l);
            const nv = tmp();
            out.push("  " + nv + " = " + (e.op === "++" ? "add" : "sub") +
                " i64 " + v + ", 1");
            out.push("  store i64 " + nv + ", i64* " + l);
            out.push("  " + r + " = add i64 0, " + nv);
            return r;
        }
        case "call":
            if (e.name === "print")
                throw new Error("print() must be a statement");
            if (e.name === "sys_exit") {
                const v = expr(e.args[0], out);
                out.push("  call void @io_exit(i64 " + v + ")");
                out.push("  " + r + " = add i64 0, 0");
                return r;
            }
            if (e.name === "putc") {
                const v = expr(e.args[0], out);
                out.push("  call void @io_putc(i64 " + v + ")");
                out.push("  " + r + " = add i64 0, 0");
                return r;
            }
            out.push("  " + r + " = call i64 @" + fname(e.name) + "(" +
                e.args.map((a) => "i64 " + expr(a, out)).join(", ") + ")");
            return r;
        case "unop": {
            const a = expr(e.a, out);
            if (e.op === "-") {
                out.push("  " + r + " = sub i64 0, " + a);
            } else {
                const z = tmp();
                out.push("  " + z + " = icmp eq i64 " + a + ", 0");
                out.push("  " + r + " = zext i1 " + z + " to i64");
            }
            return r;
        }
        case "binop": {
            const a = expr(e.a, out);
            const b = expr(e.b, out);
            if (e.op === "&&" || e.op === "||") {
                const z = tmp();
                const az = tmp(), bz = tmp();
                out.push("  " + az + " = icmp ne i64 " + a + ", 0");
                out.push("  " + bz + " = icmp ne i64 " + b + ", 0");
                out.push("  " + z + " = " + (e.op === "&&" ? "and" : "or") +
                    " i1 " + az + ", " + bz);
                out.push("  " + r + " = zext i1 " + z + " to i64");
                return r;
            }
            if (["<", "<=", ">", ">=", "==", "!="].includes(e.op)) {
                const pred = { "<": "slt", "<=": "sle", ">": "sgt", ">=": "sge",
                               "==": "eq", "!=": "ne" }[e.op];
                const z = tmp();
                out.push("  " + z + " = icmp " + pred + " i64 " + a + ", " + b);
                out.push("  " + r + " = zext i1 " + z + " to i64");
                return r;
            }
            const op = { "+": "add", "-": "sub", "*": "mul", "/": "sdiv",
                         "%": "srem" }[e.op];
            if (!op) throw new Error("llvm: unsupported binop " + e.op);
            out.push("  " + r + " = " + op + " i64 " + a + ", " + b);
            return r;
        }
        default: throw new Error("llvm: unsupported expr " + e.kind);
        }
    }

    function stmts(list, out) {
        for (const s of list) {
            switch (s.kind) {
            case "assign": {
                const ptr = tmp() + "_p";
                out.push("  " + ptr + " = alloca i64");
                locals.set(s.name, ptr);
                if (s.value !== null) {
                    const v = expr(s.value, out);
                    out.push("  store i64 " + v + ", i64* " + ptr);
                } else {
                    out.push("  store i64 0, i64* " + ptr);
                }
                break;
            }
            case "block": stmts(s.stmts, out); break;
            case "if": {
                const c = expr(s.cond, out);
                const z = tmp();
                out.push("  " + z + " = icmp ne i64 " + c + ", 0");
                const E = tmp() + "_l", X = tmp() + "_l";
                out.push("  br i1 " + z + ", label " + X + ", label " + E);
                out.push(X.slice(1) + ":");
                stmts([s.then], out);
                if (s.els) {
                    const D = tmp() + "_l";
                    out.push("  br label " + D);
                    out.push(E.slice(1) + ":");
                    stmts([s.els], out);
                    out.push("  br label " + D);
                    out.push(D.slice(1) + ":");
                } else {
                    out.push("  br label " + E);
                    out.push(E.slice(1) + ":");
                }
                break;
            }
            case "while": {
                const T = tmp() + "_l", E = tmp() + "_l", C = tmp() + "_l";
                out.push("  br label " + T);
                out.push(T.slice(1) + ":");
                if (s.cond) {
                    const c = expr(s.cond, out);
                    const z = tmp();
                    out.push("  " + z + " = icmp ne i64 " + c + ", 0");
                    out.push("  br i1 " + z + ", label " + C + ", label " + E);
                    out.push(C.slice(1) + ":");
                }
                stmts([s.body], out);
                out.push("  br label " + T);
                out.push(E.slice(1) + ":");
                break;
            }
            case "for": {
                if (s.init && s.init.kind === "assign") {
                    const ptr = tmp() + "_p";
                    out.push("  " + ptr + " = alloca i64");
                    locals.set(s.init.name, ptr);
                    const v = expr(s.init.value, out);
                    out.push("  store i64 " + v + ", i64* " + ptr);
                } else if (s.init) {
                    expr(s.init, out);
                }
                const T = tmp() + "_l", E = tmp() + "_l", C = tmp() + "_l";
                out.push("  br label " + T);
                out.push(T.slice(1) + ":");
                if (s.cond) {
                    const c = expr(s.cond, out);
                    const z = tmp();
                    out.push("  " + z + " = icmp ne i64 " + c + ", 0");
                    out.push("  br i1 " + z + ", label " + C + ", label " + E);
                    out.push(C.slice(1) + ":");
                }
                stmts([s.body], out);
                if (s.upd) expr(s.upd, out);
                out.push("  br label " + T);
                out.push(E.slice(1) + ":");
                break;
            }
            case "return":
                if (s.value) {
                    const v = expr(s.value, out);
                    out.push("  ret i64 " + v);
                } else {
                    out.push("  ret i64 0");
                }
                return;
            case "expr": {
                const lit = js_print_literal(s.expr);
                if (lit !== null) {
                    const idx = strings.indexOf(lit);
                    out.push("  call void @io_puts(i8* getelementptr inbounds ([" +
                        (lit.length + 1) + " x i8], [" + (lit.length + 1) +
                        " x i8]* @js_msg_" + idx + ", i64 0, i64 0), i64 " +
                        lit.length + ")");
                } else {
                    expr(s.expr, out);
                }
                break;
            }
            default: throw new Error("llvm: unsupported stmt " + s.kind);
            }
        }
    }

    const out = [];
    out.push("; generated by mbjs-to-native.js (--llvm mode)");
    out.push("target triple = \"x86_64-unknown-none\"");
    out.push("declare void @io_puts(i8*, i64)");
    out.push("declare void @io_putc(i64)");
    out.push("declare void @io_exit(i64)");
    out.push("");
    strings.forEach((s, idx) => {
        let esc = "";
        for (const ch of s) {
            const cp = ch.codePointAt(0);
            if (cp === 10) esc += "\\0A";
            else if (cp === 9) esc += "\\09";
            else if (cp === 34) esc += "\\22";
            else if (cp === 92) esc += "\\5C";
            else if (cp < 32 || cp > 126) esc += "\\" + cp.toString(16).padStart(2, "0");
            else esc += ch;
        }
        out.push("@js_msg_" + idx + " = private unnamed_addr constant [" +
            (s.length + 1) + " x i8] c\"" + esc + "\\00\"");
    });
    out.push("");
    for (const f of ast.funcs) {
        locals.clear();
        out.push("define i64 @" + fname(f.name) + "(" +
            f.params.map((p) => "i64 %" + js_sym(p)).join(", ") + ") {");
        f.params.forEach((p) => {
            const ptr = tmp() + "_p";
            out.push("  " + ptr + " = alloca i64");
            out.push("  store i64 %" + js_sym(p) + ", i64* " + ptr);
            locals.set(p, ptr);
        });
        const body = [];
        stmts(f.body, body);
        out.push(...body);
        out.push("  ret i64 0");
        out.push("}");
        out.push("");
    }
    out.push("define i64 @main() {");
    out.push("  %r = call i64 @fn_main()");
    out.push("  ret i64 %r");
    out.push("}");
    return out.join("\n") + "\n";
}

/* ------------------------------------------------------------------ */
/* --c3 mode: C3 language with inline asm blocks (c3c)                 */
/* ------------------------------------------------------------------ */

function js_emit_c3(ast) {
    const locals = new Map();
    const fname = (name) => "fn_" + js_sym(name);

    function expr(e) {
        switch (e.kind) {
        case "num": return e.value.toString() + "ll";
        case "var": {
            if (!locals.has(e.name))
                throw new Error("c3: undefined var " + e.name);
            return locals.get(e.name);
        }
        case "call":
            if (e.name === "print")
                throw new Error("print() must be a statement");
            if (e.name === "putc") return "io_putc(" + expr(e.args[0]) + ")";
            if (e.name === "sys_exit") return "io_exit(" + expr(e.args[0]) + ")";
            return fname(e.name) + "(" + e.args.map((a) => expr(a)).join(", ") + ")";
        case "postinc":
        case "postdec":
            if (!locals.has(e.name))
                throw new Error("c3: undefined var " + e.name);
            return locals.get(e.name) + (e.op === "++" ? "++" : "--");
        case "unop":
            return e.op === "-" ? "(-(" + expr(e.a) + "))" : "(!(" + expr(e.a) + "))";
        case "binop":
            if (e.op === "&&") return "((" + expr(e.a) + ") && (" + expr(e.b) + "))";
            if (e.op === "||") return "((" + expr(e.a) + ") || (" + expr(e.b) + "))";
            return "((" + expr(e.a) + ") " + e.op + " (" + expr(e.b) + "))";
        default: throw new Error("c3: unsupported expr " + e.kind);
        }
    }
    function mklocal(name) {
        if (!locals.has(name)) locals.set(name, js_mangle(name));
        return locals.get(name);
    }

    function stmts(list, out) {
        for (const s of list) {
            switch (s.kind) {
            case "assign":
                if (s.value === null)
                    out.push("    long " + mklocal(s.name) + " = 0;");
                else if (s.decl === false)
                    out.push("    " + locals.get(s.name) + " = " + expr(s.value) + ";");
                else
                    out.push("    long " + mklocal(s.name) + " = " + expr(s.value) + ";");
                break;
            case "block": out.push("    {"); stmts(s.stmts, out); out.push("    }"); break;
            case "if":
                out.push("    if (" + expr(s.cond) + ") {");
                stmts([s.then], out);
                if (s.els) { out.push("    } else {"); stmts([s.els], out); }
                out.push("    }");
                break;
            case "while":
                out.push("    while (" + expr(s.cond) + ") {");
                stmts([s.body], out);
                out.push("    }");
                break;
            case "for":
                if (s.init && s.init.kind === "assign")
                    out.push("    long " + mklocal(s.init.name) + " = " +
                        expr(s.init.value) + ";");
                out.push("    for (; " + (s.cond ? expr(s.cond) : "1") + "; " +
                    (s.upd ? expr(s.upd) : "") + ") {");
                stmts([s.body], out);
                out.push("    }");
                break;
            case "return":
                out.push("    return " + (s.value ? expr(s.value) : "0") + ";");
                break;
            case "expr": {
                const lit = js_print_literal(s.expr);
                if (lit !== null)
                    out.push("    io_puts(\"" + lit.replace(/\\/g, "\\\\")
                        .replace(/"/g, '\\"').replace(/\n/g, "\\n")
                        .replace(/\t/g, "\\t") + "\", " + lit.length + ");");
                else
                    out.push("    " + expr(s.expr) + ";");
                break;
            }
            default: throw new Error("c3: unsupported stmt " + s.kind);
            }
        }
    }

    const out = [];
    out.push("/* generated by mbjs-to-native.js (--c3 mode) — C3 language (c3c) */");
    out.push("module mbjs_aot;");
    out.push("import std::io;");
    out.push("");
    out.push("/* freestanding inline assembly: Berry OS syscall hooks");
    out.push(" * (asm blocks use C3's common grammar; registers are $prefixed) */");
    out.push("fn void io_putc(char c) {");
    out.push("    asm {");
    out.push("        movl $eax, c;");
    out.push("    }");
    out.push("    /* replace with the platform syscall (SYS_BERRY_SOW) */");
    out.push("}");
    out.push("fn void io_exit(long code) {");
    out.push("    asm {");
    out.push("        movl $eax, code;");
    out.push("    }");
    out.push("    /* replace with the platform syscall (SYS_BERRY_WITHER) */");
    out.push("}");
    out.push("fn void io_puts(char* s, long len) { io::printf(\"%s\", s); }");
    out.push("");
    for (const f of ast.funcs) {
        locals.clear();
        f.params.forEach((p) => mklocal(p));
        out.push("fn long " + fname(f.name) + "(" +
            f.params.map((p) => "long " + locals.get(p)).join(", ") + ") {");
        stmts(f.body, out);
        out.push("}");
        out.push("");
    }
    out.push("fn long main() {");
    out.push("    return fn_main();");
    out.push("}");
    return out.join("\n") + "\n";
}

/* ------------------------------------------------------------------ */
/* --basm mode: Berry assembler (.basm) for beasm -> Berry binaries    */
/* ------------------------------------------------------------------ */

function js_emit_basm(ast) {
    /* NOTE: This is the x86-64 basm emitter. For ARM32/ARM64/RISC-V
     * targets, use --gas-arm32, --gas-arm64, --gas-rv32/64/128 which
     * import the dedicated emitters from platform/arch/.
     */
    const locals = new Map();   /* var -> stack offset (positive, from rbp) */
    const fname = (name) => "fn_" + js_sym(name);
    const strings = js_print_strings(ast);
    let js_stack = 0;
    let js_label = 0;
    const jl = () => ".L" + (js_label++);

    function alloc(name) {
        if (!locals.has(name)) {
            js_stack += 8;
            locals.set(name, js_stack);
        }
        return locals.get(name);
    }

    function expr(e, out) {
        switch (e.kind) {
        case "num":
            out.push("    mov rax, " + e.value);
            return;
        case "var":
            out.push("    mov rax, [rbp-" + alloc(e.name) + "]");
            return;
        case "postinc":
        case "postdec": {
            const off = alloc(e.name);
            out.push("    mov rax, [rbp-" + off + "]");
            out.push("    " + (e.op === "++" ? "add rax, 1" : "sub rax, 1"));
            out.push("    mov [rbp-" + off + "], rax");
            return;
        }
        case "call":
            if (e.name === "print")
                throw new Error("print() must be a statement");
            if (e.name === "sys_exit") {
                expr(e.args[0], out);
                out.push("    mov rdi, rax");
                out.push("    mov rax, 0x05");   /* SYS_BERRY_WITHER */
                out.push("    syscall");
                return;
            }
            if (e.name === "putc") {
                expr(e.args[0], out);
                out.push("    push rax");
                out.push("    mov rdi, 1");
                out.push("    lea rsi, [rsp]");
                out.push("    mov rdx, 1");
                out.push("    mov rax, 0x02");   /* SYS_BERRY_SOW */
                out.push("    syscall");
                out.push("    pop rax");
                return;
            }
            /* push args right-to-left so the leftmost ends up on top */
            for (let i = e.args.length - 1; i >= 0; i--) {
                expr(e.args[i], out);
                out.push("    push rax");
            }
            {
                const regs = ["rdi", "rsi", "rdx", "rcx", "r8", "r9"];
                for (let i = 0; i < Math.min(e.args.length, 6); i++)
                    out.push("    pop " + regs[i]);
            }
            out.push("    call " + fname(e.name));
            return;
        case "unop":
            if (e.op === "-") {
                expr(e.a, out);
                out.push("    neg rax");
            } else {
                expr(e.a, out);
                out.push("    test rax, rax");
                out.push("    sete al");
                out.push("    movzx rax, al");
            }
            return;
        case "binop": {
            if (e.op === "&&" || e.op === "||") {
                const T = jl(), E = jl(), D = jl();
                expr(e.a, out);
                out.push("    test rax, rax");
                out.push(e.op === "&&" ? "    jz " + E : "    jnz " + T);
                expr(e.b, out);
                out.push("    test rax, rax");
                out.push(e.op === "&&" ? "    jz " + E : "    jnz " + T);
                out.push("    mov rax, 1");
                out.push("    jmp " + D);
                out.push(E + ":");
                out.push("    xor rax, rax");
                out.push(D + ":");
                return;
            }
            expr(e.b, out);
            out.push("    push rax");
            expr(e.a, out);
            out.push("    pop rcx");
            switch (e.op) {
            case "+": out.push("    add rax, rcx"); break;
            case "-": out.push("    sub rax, rcx"); break;
            case "*": out.push("    imul rax, rcx"); break;
            case "/":
                /* beasm has no idiv: use the shift-based helper.
                 * the helper already applies the dividend's sign to the
                 * quotient; flip once more if the divisor was negative. */
                {
                    const Q = jl();
                    out.push("    mov r8, rcx");
                    out.push("    call __mbjs_divmod");
                    out.push("    cmp r8, 0");
                    out.push("    jge " + Q);
                    out.push("    neg rax");
                    out.push(Q + ":");
                }
                break;
            case "%":
                /* remainder takes the dividend's sign (JS semantics) */
                {
                    const R = jl();
                    out.push("    mov r8, rax");
                    out.push("    call __mbjs_divmod");
                    out.push("    mov rax, rdx");
                    out.push("    cmp r8, 0");
                    out.push("    jge " + R);
                    out.push("    neg rax");
                    out.push(R + ":");
                }
                break;
            default: {
                const cc = { "<": "setl", "<=": "setle", ">": "setg",
                             ">=": "setge", "==": "sete", "!=": "setne" }[e.op];
                if (!cc) throw new Error("basm: unsupported binop " + e.op);
                out.push("    cmp rax, rcx");
                out.push("    " + cc + " al");
                out.push("    movzx rax, al");
                break;
            }
            }
            return;
        }
        default:
            throw new Error("basm: unsupported expr " + e.kind);
        }
    }

    function stmts(list, out) {
        for (const s of list) {
            switch (s.kind) {
            case "assign":
                alloc(s.name);
                if (s.value === null) {
                    out.push("    mov rax, 0");
                    out.push("    mov [rbp-" + locals.get(s.name) + "], rax");
                } else {
                    expr(s.value, out);
                    out.push("    mov [rbp-" + locals.get(s.name) + "], rax");
                }
                break;
            case "block": stmts(s.stmts, out); break;
            case "if": {
                expr(s.cond, out);
                const E = jl();
                out.push("    test rax, rax");
                out.push("    jz " + E);
                stmts([s.then], out);
                if (s.els) {
                    const X = jl();
                    out.push("    jmp " + X);
                    out.push(E + ":");
                    stmts([s.els], out);
                    out.push(X + ":");
                } else {
                    out.push(E + ":");
                }
                break;
            }
            case "while": {
                const T = jl(), E = jl();
                out.push(T + ":");
                expr(s.cond, out);
                out.push("    test rax, rax");
                out.push("    jz " + E);
                stmts([s.body], out);
                out.push("    jmp " + T);
                out.push(E + ":");
                break;
            }
            case "for": {
                if (s.init && s.init.kind === "assign") {
                    alloc(s.init.name);
                    expr(s.init.value, out);
                    out.push("    mov [rbp-" + locals.get(s.init.name) + "], rax");
                } else if (s.init) expr(s.init, out);
                const T = jl(), E = jl();
                out.push(T + ":");
                if (s.cond) {
                    expr(s.cond, out);
                    out.push("    test rax, rax");
                    out.push("    jz " + E);
                }
                stmts([s.body], out);
                if (s.upd) expr(s.upd, out);
                out.push("    jmp " + T);
                out.push(E + ":");
                break;
            }
            case "return":
                if (s.value) expr(s.value, out);
                else out.push("    xor rax, rax");
                out.push("    leave");
                out.push("    ret");
                return;
            case "expr": {
                const lit = js_print_literal(s.expr);
                if (lit !== null) {
                    const idx = strings.indexOf(lit);
                    out.push("    lea rsi, [rel js_msg_" + idx + "]");
                    out.push("    mov rdx, " + lit.length);
                    out.push("    mov rdi, 1");
                    out.push("    mov rax, 0x02");   /* SYS_BERRY_SOW */
                    out.push("    syscall");
                } else {
                    expr(s.expr, out);
                }
                break;
            }
            default:
                throw new Error("basm: unsupported stmt " + s.kind);
            }
        }
    }

    const out = [];
    out.push("; generated by mbjs-to-native.js (--basm mode)");
    out.push("; Berry OS x86-64 assembly for beasm (SYS_BERRY_SOW=0x02, SYS_BERRY_WITHER=0x05)");
    out.push("");
    out.push("section '.text' code executable");
    out.push("");
    out.push("_start:");
    out.push("    call fn_main");
    out.push("    mov rdi, rax");
    out.push("    mov rax, 0x05");
    out.push("    syscall");
    out.push("");

    for (const f of ast.funcs) {
        locals.clear();
        js_stack = 0;
        /* pre-scan: reserve stack for every local before any push/pop */
        js_collect_locals(f).forEach((n) => alloc(n));
        const frame = (js_stack + 15) & ~15;
        out.push(fname(f.name) + ":");
        out.push("    push rbp");
        out.push("    mov rbp, rsp");
        if (frame > 0)
            out.push("    sub rsp, " + frame);
        for (let i = 0; i < f.params.length; i++)
            out.push("    mov [rbp-" + alloc(f.params[i]) + "], " +
                ["rdi", "rsi", "rdx", "rcx", "r8", "r9"][i]);
        stmts(f.body, out);
        out.push("    xor rax, rax");
        out.push("    leave");
        out.push("    ret");
        out.push("");
    }

    if (strings.length) {
        out.push("section '.rodata' data readable");
        out.push("");
        strings.forEach((s, idx) => {
            const esc = s.replace(/'/g, "\\'");
            out.push("js_msg_" + idx + " db '" + esc + "', 0");
        });
        out.push("");
    }

    out.push("section '.exports' data readable");
    out.push("BERRY_EXPORT _start, 1");
    out.push("dd 0, 0, 0");
    out.push("");
    out.push("; ----------------------------------------------------------------");
    out.push("; __mbjs_divmod — signed 64-bit division by shift-subtraction");
    out.push("; (beasm has no idiv). in: rax=n, rcx=d; out: rax=n/d, rdx=n%d");
    out.push("; (JS semantics: truncation toward zero, remainder takes the");
    out.push(";  sign of the dividend)");
    out.push("; locals: [rbp-8]=n [rbp-16]=d [rbp-24]=q [rbp-32]=r");
    out.push(";         [rbp-40]=n_neg [rbp-48]=bit [rbp-56]=d_neg");
    out.push("section '.text' code executable");
    out.push("");
    out.push("__mbjs_divmod:");
    out.push("    push rbp");
    out.push("    mov rbp, rsp");
    out.push("    sub rsp, 56");
    out.push("    mov [rbp-8], rax");
    out.push("    mov [rbp-16], rcx");
    out.push("    mov rax, 0");
    out.push("    mov [rbp-24], rax");
    out.push("    mov rax, 0");
    out.push("    mov [rbp-32], rax");
    out.push("    mov rax, 0");
    out.push("    mov [rbp-40], rax");
    out.push("    mov rax, 0");
    out.push("    mov [rbp-56], rax");
    out.push("    mov rax, [rbp-8]");
    out.push("    cmp rax, 0");
    out.push("    jge .ld0");
    out.push("    neg rax");
    out.push("    mov [rbp-8], rax");
    out.push("    mov rax, 1");
    out.push("    mov [rbp-40], rax");
    out.push(".ld0:");
    out.push("    mov rax, [rbp-16]");
    out.push("    cmp rax, 0");
    out.push("    jge .ld1");
    out.push("    neg rax");
    out.push("    mov [rbp-16], rax");
    out.push("    mov rax, 1");
    out.push("    mov [rbp-56], rax");
    out.push(".ld1:");
    out.push("    mov rax, [rbp-16]");
    out.push("    cmp rax, 0");
    out.push("    jne .ld2");
    out.push("    xor rax, rax");
    out.push("    xor rdx, rdx");
    out.push("    leave");
    out.push("    ret");
    out.push(".ld2:");
    out.push("    mov rax, 62");
    out.push("    mov [rbp-48], rax");
    out.push(".ldloop:");
    out.push("    mov rax, [rbp-48]");
    out.push("    cmp rax, 0");
    out.push("    jl .lddone");
    out.push("    mov rax, [rbp-32]");
    out.push("    shl rax, 1");
    out.push("    mov [rbp-32], rax");
    out.push("    mov rax, [rbp-8]");
    out.push("    mov rcx, [rbp-48]");
    out.push("    shr rax, cl");
    out.push("    and rax, 1");
    out.push("    mov rcx, [rbp-32]");
    out.push("    or rcx, rax");
    out.push("    mov [rbp-32], rcx");
    out.push("    mov rax, [rbp-32]");
    out.push("    mov rcx, [rbp-16]");
    out.push("    cmp rax, rcx");
    out.push("    jl .ldnext");
    out.push("    sub rax, rcx");
    out.push("    mov [rbp-32], rax");
    out.push("    mov rax, 1");
    out.push("    mov rcx, [rbp-48]");
    out.push("    shl rax, cl");
    out.push("    mov rcx, [rbp-24]");
    out.push("    or rcx, rax");
    out.push("    mov [rbp-24], rcx");
    out.push(".ldnext:");
    out.push("    mov rax, [rbp-48]");
    out.push("    dec rax");
    out.push("    mov [rbp-48], rax");
    out.push("    jmp .ldloop");
    out.push(".lddone:");
    out.push("    mov rax, [rbp-40]");
    out.push("    cmp rax, 0");
    out.push("    je .ldpos");
    out.push("    mov rax, [rbp-24]");
    out.push("    neg rax");
    out.push("    mov [rbp-24], rax");
    out.push(".ldpos:");
    out.push("    mov rax, [rbp-24]");
    out.push("    mov rdx, [rbp-32]");
    out.push("    leave");
    out.push("    ret");
    return out.join("\n") + "\n";
}

/* ------------------------------------------------------------------ */
/* Inline Assembly Scanner                                             */
/* ------------------------------------------------------------------ */

/* Scan source for @asm(...), <__ ... __>, and asm`...` blocks.
 * The three modes:
 *   @asm(...)          - RAW: verbatim, no optimization
 *   <__ ... __>        - OPTIMIZED: variable resolution + register alloc
 *   asm`...`            - TEMPLATE: JS expression interpolation
 */

function js_scan_inline_asm(source) {
    var blocks = [];
    var i = 0;
    var len = source.length;

    while (i < len) {
        /* --- @asm(...) --- */
        var idx = source.indexOf("@asm", i);
        if (idx !== -1 && (idx === 0 || !/[a-zA-Z0-9_]/.test(source[idx - 1]))) {
            var j = idx + 4;
            while (j < len && /\s/.test(source[j])) j++;
            var isVolatile = false;
            if (source.substr(j, 8) === "volatile") {
                isVolatile = true;
                j += 8;
                while (j < len && /\s/.test(source[j])) j++;
            }
            if (source[j] === "(") {
                j++;
                var depth = 1;
                var content = "";
                var inQ = false;
                var qCh = "";
                while (j < len && depth > 0) {
                    var ch = source[j];
                    if (inQ) {
                        if (ch === qCh && source[j - 1] !== "\\") inQ = false;
                        else content += ch;
                    } else {
                        if (ch === '"' || ch === "'" || ch === "`") {
                            inQ = true;
                            qCh = ch;
                        } else if (ch === "(") depth++;
                        else if (ch === ")") depth--;
                        if (depth > 0) content += ch;
                    }
                    j++;
                }
                content = content.replace(/^\s*[`"']+|[`"']+\s*$/g, "").trim();
                blocks.push({ type: "raw", content: content, volatile: isVolatile, start: idx, end: j });
                i = j;
                continue;
            }
        }

        /* --- <__ ... __> --- */
        idx = source.indexOf("<__", i);
        if (idx !== -1) {
            var endIdx = source.indexOf("__>", idx + 3);
            if (endIdx !== -1) {
                blocks.push({ type: "optimized", content: source.slice(idx + 3, endIdx).trim(), start: idx, end: endIdx + 3 });
                i = endIdx + 3;
                continue;
            }
        }

        /* --- asm`...` --- */
        idx = source.indexOf("asm`", i);
        if (idx !== -1 && (idx === 0 || !/[a-zA-Z0-9_]/.test(source[idx - 1]))) {
            var j2 = idx + 4;
            while (j2 < len && source[j2] === " ") j2++;
            if (source[j2] === "`") {
                j2++;
                var tplContent = "";
                while (j2 < len && source[j2] !== "`") {
                    tplContent += source[j2];
                    j2++;
                }
                j2++;
                blocks.push({ type: "template", content: tplContent.trim(), start: idx, end: j2 });
                i = j2;
                continue;
            }
        }

        i++;
    }
    return blocks;
}

function js_strip_inline_asm(source, blocks) {
    var result = source;
    for (var k = blocks.length - 1; k >= 0; k--) {
        result = result.slice(0, blocks[k].start) + result.slice(blocks[k].end);
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* Driver                                                              */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Driver                                                              */
/* ------------------------------------------------------------------ */

const js_src = std.loadFile(js_src_file);
if (js_src === null) {
    print("mbjs-to-native: cannot read " + js_src_file);
    std.exit(1);
}
let js_ast;
try {

} catch (e) {
    print("mbjs-to-native: parse error in " + js_src_file + ": " + e.message);
    std.exit(1);
}

/* Scan for inline assembly blocks */
const js_asm_blocks = js_scan_inline_asm(js_src);

/* If inline asm found and we're in basm mode, emit asm sections first */
let js_asm_basm = "";
if (js_asm_blocks.length > 0 && js_mode === "--basm") {
    js_asm_basm = "; === Inline Assembly Blocks ===\n";
    for (const b of js_asm_blocks) {
        if (b.type === "raw") {
            js_asm_basm += "; @asm" + (b.volatile ? " volatile" : "") + " (raw, verbatim)\n";
            js_asm_basm += b.content.split("\n").map(function(l) { return "    " + l; }).join("\n") + "\n\n";
        } else if (b.type === "optimized") {
            js_asm_basm += "; <__ optimized __>\n";
            js_asm_basm += b.content.split("\n").map(function(l) { return "    " + l; }).join("\n") + "\n\n";
        } else if (b.type === "template") {
            js_asm_basm += "; asm template\n";
            js_asm_basm += b.content.split("\n").map(function(l) { return "    " + l; }).join("\n") + "\n\n";
        }
    }
}

/* Strip inline asm from source for normal JS parsing */
/* Strip inline asm from source for normal JS parsing */
const js_src_clean = js_asm_blocks.length > 0 ? js_strip_inline_asm(js_src, js_asm_blocks) : js_src;
js_ast = js_parse(js_tokenize(js_src_clean));

let js_result;
switch (js_mode) {
case "--c":    js_result = js_emit_c(js_ast); break;
case "--c3":   js_result = js_emit_c3(js_ast); break;
case "--gas":  js_result = js_emit_gas(js_ast); break;
case "--llvm": js_result = js_emit_llvm(js_ast); break;
case "--basm": js_result = js_asm_basm + js_emit_basm(js_ast); break;
/* Multi-architecture GAS emitters from platform/arch/ */
case "--gas-arm32": {
    const { emit_arm32_gas } = await import("../platform/arch/arm32.js");
    js_result = emit_arm32_gas(js_ast);
    break;
}
case "--gas-arm64": {
    const { emit_arm64_gas } = await import("../platform/arch/arm64.js");
    js_result = emit_arm64_gas(js_ast);
    break;
}
case "--gas-rv32": {
    const { emit_rv32_gas } = await import("../platform/arch/riscv.js");
    js_result = emit_rv32_gas(js_ast);
    break;
}
case "--gas-rv64": {
    const { emit_rv64_gas } = await import("../platform/arch/riscv.js");
    js_result = emit_rv64_gas(js_ast);
    break;
}
case "--gas-rv128": {
    const { emit_rv128_gas } = await import("../platform/arch/riscv.js");
    js_result = emit_rv128_gas(js_ast);
    break;
}
default: js_usage();
}

if (js_out_file !== null) {
    const f = std.open(js_out_file, "w");
    if (f === null) {
        print("mbjs-to-native: cannot write " + js_out_file);
        std.exit(1);
    }
    f.puts(js_result);
    f.close();
    print("mbjs-to-native: wrote " + js_out_file + " (" + js_result.length +
        " bytes, " + js_mode + ")");
} else {
    print(js_result);
}
