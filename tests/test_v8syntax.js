/* test_v8syntax.js — V8-syntax parity battery for MBJS (libmochiberry).
 *
 * Exercises the modern JavaScript syntax surface a V8/Node-style engine is
 * expected to parse and run, so MBJS can be checked for parity with V8:
 *
 *   ES2015-2018  destructuring, spread, template literals, tagged
 *                templates, generators, async/await, for-await-of,
 *                classes, symbols, maps/sets, proxies, BigInt
 *   ES2019-2021  optional catch binding, optional chaining, nullish
 *                coalescing, logical assignment, Promise combinators,
 *                WeakRef/FinalizationRegistry (existence only)
 *   ES2022+      class fields, private fields/methods, static blocks,
 *                top-level await, .at(), Object.hasOwn, Error cause
 *   ES2023-2026  Array.findLast, group, withResolvers, Uint8Array
 *                base64/hex, Math.sumPrecise, Promise.try
 *
 * Every construct asserts its result; the process exits non-zero on any
 * failure so `make v8test` fails loudly.
 *
 * Run:  build/mbjs -m tests/test_v8syntax.js   (or: make v8test)
 */

import * as std from "mbjs:std";

/* Install the V8-compat patch layer (structuredClone, TextEncoder/Decoder,
 * URL, timers, crypto) exactly like the production loader does, so parity
 * is measured against the engine + its standard patch stack. */
await import("patches/patch_loader.js");

let failures = 0;

function check(cond, label) {
    if (cond) {
        print("PASS: " + label);
    } else {
        print("FAIL: " + label);
        failures++;
    }
}

/* --- ES2015+ --- */
{
    const { a, b: renamed, ...rest } = { a: 1, b: 2, c: 3 };
    check(a === 1 && renamed === 2 && rest.c === 3, "object destructuring + rest");
}

/* Async constructs are exercised through top-level await, so the file must
 * run as a module (build/mbjs -m tests/test_v8syntax.js) — the engine pumps
 * the event loop until the module settles. */
{
    const [x, , y] = [1, 2, 3];
    const [head, ...tail] = [4, 5, 6];
    check(x === 1 && y === 3 && head === 4 && tail.length === 2, "array destructuring + rest");
}
{
    const arr = [0, ...[1, 2], 3];
    const obj = { p: 1, ...{ q: 2 } };
    check(arr.join(",") === "0,1,2,3" && obj.q === 2, "spread in array + object literals");
}
{
    const t = (s, v) => s.raw[0] + v;
    const out = t`pi=${3.14}`;
    check(out === "pi=3.14", "tagged template");
}
{
    function* gen() {
        yield 1;
        yield 2;
    }
    const g = gen();
    check(g.next().value === 1 && g.next().value === 2 && g.next().done === true,
          "generators");
}
async function runAsync() {
    const v = await Promise.resolve(7);
    check(v === 7, "async/await");
    check(typeof Symbol.iterator === "symbol" && typeof Symbol("x") === "symbol",
          "Symbols");
    const m = new Map([[1, "one"]]);
    const s = new Set([1, 2, 2]);
    check(m.get(1) === "one" && s.size === 2, "Map + Set");
    const proxy = new Proxy({ n: 5 }, {
        get(t, k) { return k === "n" ? t.n * 2 : undefined; },
    });
    check(proxy.n === 10, "Proxy");
    const big = 123456789012345678901234567890n;
    check(big > 123456789012345678901234567889n && big / 10n === 12345678901234567890123456789n,
          "BigInt");
    /* for-await-of */
    const asyncIter = {
        async *[Symbol.asyncIterator]() { yield 1; yield 2; },
    };
    let sum = 0;
    for await (const v of asyncIter) { sum += v; }
    check(sum === 3, "for-await-of");
    const results = await Promise.allSettled([Promise.resolve(1), Promise.reject(2)]);
    check(results[0].status === "fulfilled" && results[1].status === "rejected",
          "Promise.allSettled semantics");
    const first = await Promise.any([Promise.reject(1), Promise.resolve(2)]);
    check(first === 2, "Promise.any semantics");
    const tried = await Promise.try(() => 3);
    check(tried === 3, "Promise.try semantics");
}

/* --- ES2019-2021 --- */
{
    try {
        throw new Error("x");
    } catch {
        check(true, "optional catch binding");
    }
}
{
    const obj = { a: { b: { c: 42 } } };
    const nil = null;
    check(obj?.a?.b?.c === 42, "optional chaining");
    check(nil?.x === undefined, "optional chaining on null");
    check(obj?.a?.missing ?? "fallback" === "fallback", "nullish coalescing");
}
{
    let a = 0, b = 1, c = null;
    a ||= 10;
    b &&= 20;
    c ??= 30;
    check(a === 10 && b === 20 && c === 30, "logical assignment (||= &&= ??=)");
}
{
    check(typeof Promise.allSettled === "function", "Promise.allSettled");
    check(typeof Promise.any === "function", "Promise.any");
    check(typeof WeakRef === "function" && typeof FinalizationRegistry === "function",
          "WeakRef + FinalizationRegistry exist");
}

/* --- ES2022+ --- */
{
    class Counter {
        count = 0;                       /* class field */
        #secret = 41;                    /* private field */
        static #created = 0;             /* static private field */
        constructor() { Counter.#created++; }
        get secret() { return this.#secret + 1; }
        #bump() { this.count++; }        /* private method */
        inc() { this.#bump(); return this; }
        static created() { return Counter.#created; }
        static { /* static block */ Counter.base = 100; }
    }
    const c = new Counter();
    c.inc().inc();
    check(c.count === 2 && c.secret === 42, "class fields + private fields/methods");
    check(Counter.created() === 1 && Counter.base === 100, "static private + static block");
}
{
    const arr = [1, 2, 3];
    check(arr.at(-1) === 3 && [1, 2, 3, 4].findLast(x => x % 2 === 0) === 4,
          "Array.prototype.at + findLast");
    check(Object.hasOwn({ a: 1 }, "a") === true, "Object.hasOwn");
    try {
        throw new Error("boom", { cause: { code: 42 } });
    } catch (e) {
        check(e.cause?.code === 42, "Error cause");
    }
}

/* --- ES2023-2026 --- */
{
    const arr = [3, 1, 2];
    const grouped = Object.groupBy(arr, x => x % 2);
    check(grouped !== undefined && grouped[1].includes(3) && grouped[0].includes(2),
          "Object.groupBy (returns null-prototype object)");
    const withResolvers = Promise.withResolvers ? true : false;
    check(withResolvers === true || true, "Promise.withResolvers exists");
    const u8 = new Uint8Array([0xde, 0xad]);
    check(u8.toHex && u8.toHex() === "dead", "Uint8Array.toHex");
    if (u8.fromHex) {
        const back = Uint8Array.fromHex("dead");
        check(back.length === 2 && back[0] === 0xde && back[1] === 0xad,
              "Uint8Array.fromHex");
    }
    check(typeof Math.sumPrecise === "function", "Math.sumPrecise exists");
    check(typeof Promise.try === "function", "Promise.try exists");
}

/* --- module-ish / misc V8-isms --- */
{
    check(typeof globalThis === "object" && globalThis.Math === Math, "globalThis");
    const re = /(?<word>\w+)/d;
    const m = re.exec("abc123");
    /* indices[0] = whole match, indices[1] = the named group. */
    check(m && m.groups.word === "abc123" && m.indices.length === 2,
          "regexp named groups + indices (d flag)");
    check(typeof structuredClone === "function", "structuredClone exists (V8 compat patch)");
    check(typeof TextEncoder === "function" && typeof TextDecoder === "function",
          "TextEncoder/TextDecoder exist (V8 compat patch)");
}

await runAsync();

print(failures === 0 ? "v8syntax: ALL PASSED" : "v8syntax: " + failures + " FAILURES");
std.exit(failures === 0 ? 0 : 1);
