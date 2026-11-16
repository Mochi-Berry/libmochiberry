/*
 * test_patches.js — regression test for the pure-JS ECMA2025/2026 patches.
 *
 * Loads patches/patch_loader.js (which installs whatever the engine lacks),
 * then verifies the patched and native features behave correctly.
 *
 * Run:
 *   build/mbjs -m tests/test_patches.js
 */

import * as std from "mbjs:std";

/* Load + apply all patch_*.js files in order (CWD-relative, like the CLI). */
await import("patches/patch_loader.js");

let failures = 0;
const check = (cond, msg) => {
    if (cond) {
        console.log("PASS: " + msg);
    } else {
        console.log("FAIL: " + msg);
        failures++;
    }
};

/* --- ES2025 --- */

check(typeof Promise.try === "function", "Promise.try exists");
Promise.try(() => 21 + 21).then((v) => {
    check(v === 42, "Promise.try resolves with the callback result");
});

check(typeof RegExp.escape === "function", "RegExp.escape exists");
{
    const lit = "a.b*c/x(y)";
    const re = new RegExp(RegExp.escape(lit));
    check(re.test(lit), "RegExp.escape output still matches the literal text");
    check(!re.test("aXbYc/x(y)"), "RegExp.escape output no longer treats metacharacters specially");
}

const s1 = new Set([1, 2, 3]);
const s2 = new Set([3, 4]);
check(s1.union(s2).size === 4, "Set.prototype.union");
check(s1.intersection(s2).has(3), "Set.prototype.intersection");
check(s1.difference(s2).has(1) && !s1.difference(s2).has(3),
      "Set.prototype.difference");
check(s1.isSubsetOf(new Set([1, 2, 3, 4])), "Set.prototype.isSubsetOf");
check(s1.isSupersetOf(new Set([2])), "Set.prototype.isSupersetOf");
check(s1.isDisjointFrom(new Set([9])), "Set.prototype.isDisjointFrom");

check(typeof Math.f16round === "function", "Math.f16round exists");
check(Math.f16round(1.5) === 1.5, "Math.f16round(1.5)");
check(Math.f16round(65536) === Infinity, "Math.f16round overflows to Infinity");

{
    const r = JSON.parse('{"n": 3.5, "s": "hi"}', undefined, { source: true });
    check(r.n.source === "3.5", "JSON.parse source-access on numbers");
    check(r.s.source === '"hi"', "JSON.parse source-access on strings");
    const plain = JSON.parse('{"a":[1,2]}');
    check(Array.isArray(plain.a) && plain.a[0] === 1,
          "JSON.parse still parses normally when source is off");
}

/* --- ES2026 --- */

check(typeof String.dedent === "function", "String.dedent exists");
{
    const d = String.dedent`
        hello
          world
    `;
    check(d === "hello\n  world", "String.dedent strips common indentation");
}

check(typeof Error.isError === "function", "Error.isError exists");
check(Error.isError(new TypeError("x")), "Error.isError detects Error subclasses");
check(!Error.isError({ message: "not an error" }), "Error.isError rejects plain objects");

/* Intl is installed by the patch when the engine has none. */
check(typeof Intl !== "undefined" &&
      typeof Intl.DurationFormat === "function",
      "Intl.DurationFormat exists (Intl installed by the patch)");
if (typeof Intl !== "undefined" && Intl.DurationFormat) {
    const df = new Intl.DurationFormat();
    const out = df.format({ hours: 1, minutes: 30 });
    check(typeof out === "string" && /1/.test(out) && /30/.test(out),
          "Intl.DurationFormat formats durations");
}

/* --- ES2026 ratified features (all 7 proposals, conformance) --- */

/* 1. Array.fromAsync */
check(typeof Array.fromAsync === "function", "ES2026: Array.fromAsync exists");
{
    async function* js_pages() { yield "page 1"; yield "page 2"; }
    const pages = await Array.fromAsync(js_pages(), (p) => p.toUpperCase());
    check(pages.join(",") === "PAGE 1,PAGE 2",
          "ES2026: Array.fromAsync collects + maps async iterables");
}

/* 2. Error.isError — checked above */

/* 3. Math.sumPrecise */
check(Math.sumPrecise([1e20, 0.1, -1e20]) === 0.1,
      "ES2026: Math.sumPrecise preserves small values");

/* 4. Uint8Array base64/hex */
{
    const bytes = new Uint8Array([69, 83, 50, 48, 50, 54]); /* "ES2026" */
    const b64 = bytes.toBase64();
    check(b64 === "RVMyMDI2", "ES2026: Uint8Array.toBase64");
    const back = Uint8Array.fromBase64(b64);
    check(back.length === bytes.length &&
          back.every((b, i) => b === bytes[i]),
          "ES2026: Uint8Array.fromBase64 round-trips");
    const hex = bytes.toHex();
    check(hex === "455332303236", "ES2026: Uint8Array.toHex");
    check(Uint8Array.fromHex(hex).join() === bytes.join(),
          "ES2026: Uint8Array.fromHex round-trips");
}

/* 5. Iterator.concat (sequencing) */
{
    const seq = Iterator.concat(Iterator.from([2022, 2023]), [2024],
                                Iterator.from([2025, 2026]));
    check(Array.from(seq).join(",") === "2022,2023,2024,2025,2026",
          "ES2026: Iterator.concat sequences iterables lazily");
}

/* 6. JSON.parse source text access + JSON.rawJSON */
{
    const r = JSON.parse('{"id": 90071992547409931234567890, "count": 3}',
        (key, value, context) => {
            if (context && context.source && /^-?[0-9]+$/.test(context.source)) {
                const asNum = Number(context.source);
                if (!Number.isSafeInteger(asNum) ||
                    String(asNum) !== context.source)
                    return BigInt(context.source);
            }
            return value;
        });
    check(typeof r.id === "bigint" && r.id === 90071992547409931234567890n,
          "ES2026: JSON.parse source text recovers big integers");
    check(typeof JSON.rawJSON === "function" &&
          typeof JSON.isRawJSON === "function",
          "ES2026: JSON.rawJSON/JSON.isRawJSON exist");
    const json = JSON.stringify({ id: 123456789012345678901234567890n },
        (key, value) =>
            typeof value === "bigint" ? JSON.rawJSON(value.toString()) : value);
    check(json === '{"id":123456789012345678901234567890}',
          "ES2026: JSON.rawJSON serializes raw tokens losslessly");
}

/* 7. Map/WeakMap upsert */
{
    const m = new Map([["a", 1]]);
    check(m.getOrInsert("b", 2) === 2 && m.get("b") === 2,
          "ES2026: Map.getOrInsert inserts defaults");
    check(m.getOrInsert("a", 99) === 1,
          "ES2026: Map.getOrInsert keeps existing values");
    let called = 0;
    const v = m.getOrInsertComputed("c", (k) => { called++; return k.length; });
    check(v === 1 && called === 1,
          "ES2026: Map.getOrInsertComputed computes lazily");
    m.getOrInsertComputed("c", () => { called++; return 0; });
    check(called === 1,
          "ES2026: Map.getOrInsertComputed skips existing keys");
    const wm = new WeakMap();
    const key = {};
    check(wm.getOrInsert(key, "v") === "v",
          "ES2026: WeakMap.getOrInsert works");
    check(wm.getOrInsertComputed(key, () => "x") === "v",
          "ES2026: WeakMap.getOrInsertComputed keeps existing values");
}

/* --- ES2026 (ES17) official list — TC39 finished proposals, ratified
       2026-06-30. All seven features must be present (native or patched). */

check(typeof Math.sumPrecise === "function", "ES2026: Math.sumPrecise exists");
{
    check(Math.sumPrecise([1e20, 1, -1e20]) === 1,
          "ES2026: Math.sumPrecise survives catastrophic cancellation");
    const cents = Array(10000).fill(0.1);
    check(Math.abs(Math.sumPrecise(cents) - 1000) < 1e-9,
          "ES2026: Math.sumPrecise removes float drift");
}

check(typeof Uint8Array.prototype.toBase64 === "function" &&
      typeof Uint8Array.prototype.toHex === "function",
      "ES2026: Uint8Array base64/hex exists");
{
    const bytes = new Uint8Array([69, 83, 50, 48, 50, 54]); /* ES2026 */
    check(bytes.toBase64() === "RVMyMDI2", "ES2026: toBase64");
    check(bytes.toHex() === "455332303236", "ES2026: toHex");
    const rt = Uint8Array.fromBase64(bytes.toBase64());
    check(rt.length === 6 && rt[0] === 69, "ES2026: fromBase64 round-trip");
    const rt2 = Uint8Array.fromHex(bytes.toHex());
    check(rt2.length === 6 && rt2[5] === 54, "ES2026: fromHex round-trip");
}

check(typeof Error.isError === "function", "ES2026: Error.isError exists");

check(typeof Iterator.concat === "function", "ES2026: Iterator.concat exists");
{
    const chained = Iterator.concat(Iterator.from([1, 2]), [3, 4]);
    check(Array.from(chained).join(",") === "1,2,3,4",
          "ES2026: Iterator.concat chains iterators");
}

check(typeof Map.prototype.getOrInsert === "function" &&
      typeof Map.prototype.getOrInsertComputed === "function",
      "ES2026: Map upsert methods exist");
{
    const m = new Map();
    check(m.getOrInsert("a", 1) === 1, "ES2026: getOrInsert inserts the default");
    check(m.getOrInsert("a", 99) === 1, "ES2026: getOrInsert keeps the existing value");
    check(m.getOrInsertComputed("b", (k) => k.length) === 1,
          "ES2026: getOrInsertComputed computes on miss");
    check(m.getOrInsertComputed("b", () => 999) === 1,
          "ES2026: getOrInsertComputed does not recompute on hit");
}
check(typeof WeakMap.prototype.getOrInsert === "function",
      "ES2026: WeakMap upsert exists");
{
    const wm = new WeakMap();
    const obj = {};
    check(wm.getOrInsert(obj, 7) === 7 && wm.getOrInsert(obj, 8) === 7,
          "ES2026: WeakMap.getOrInsert");
}

check(typeof Array.fromAsync === "function", "ES2026: Array.fromAsync exists");
{
    async function* agen() { yield 1; yield 2; yield 3; }
    const arr = await Array.fromAsync(agen());
    check(arr.length === 3 && arr[2] === 3,
          "ES2026: Array.fromAsync collects async iterables");
}

/* --- V8 / Node runtime compat --- */

check(typeof structuredClone === "function", "structuredClone exists");
{
    const obj = { a: 1, b: { c: [1, 2, { d: "x" }] },
                  e: new Date(12345), f: new Map([[1, "one"]]) };
    const clone = structuredClone(obj);
    check(clone !== obj && clone.b !== obj.b && clone.b.c !== obj.b.c,
          "structuredClone returns a fresh deep tree");
    check(clone.b.c[2].d === "x" && clone.e.getTime() === 12345 &&
          clone.f.get(1) === "one", "structuredClone copies values");
    const cyc = {};
    cyc.self = cyc;
    const cyc2 = structuredClone(cyc);
    check(cyc2.self === cyc2, "structuredClone handles cycles");
}

check(typeof TextEncoder === "function" && typeof TextDecoder === "function",
      "TextEncoder/TextDecoder exist");
{
    const bytes = new TextEncoder().encode("héllo ☃");
    check(bytes instanceof Uint8Array, "TextEncoder returns Uint8Array");
    check(new TextDecoder().decode(bytes) === "héllo ☃", "UTF-8 round-trip");
    const bad = new Uint8Array([0xff, 0xfe]);
    check(new TextDecoder().decode(bad) === "\ufffd\ufffd",
          "TextDecoder replaces invalid bytes");
}

check(typeof setTimeout === "function", "setTimeout exists");
{
    const fired = await new Promise((resolve) => setTimeout(() => resolve(true), 10));
    check(fired === true, "setTimeout fires through the event loop");
}

check(typeof URL === "function" && typeof URLSearchParams === "function",
      "URL/URLSearchParams exist");
{
    const u = new URL("https://user:pw@berry.example:8443/a/b?x=1&y=%20z#frag");
    check(u.protocol === "https:" && u.hostname === "berry.example" &&
          u.port === "8443", "URL parses components");
    check(u.searchParams.get("y") === " z", "URLSearchParams decodes values");
    const sp = new URLSearchParams("a=1&a=2&b=hi");
    check(sp.getAll("a").length === 2 && sp.get("b") === "hi",
          "URLSearchParams handles duplicate keys");
}

check(typeof crypto !== "undefined" &&
      typeof crypto.getRandomValues === "function", "crypto.getRandomValues exists");
{
    const arr = new Uint8Array(16);
    crypto.getRandomValues(arr);
    check(arr.length === 16, "crypto.getRandomValues fills a typed array");
    const uuid = crypto.randomUUID();
    check(/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/
              .test(uuid), "crypto.randomUUID has the v4 format");
}

check(typeof fetch === "function", "fetch stub exists for feature detection");
{
    let rejected = false;
    fetch("https://example.com").catch(() => { rejected = true; });
    await Promise.resolve();
    check(rejected, "fetch stub rejects (no network layer)");
}

check(typeof AbortController === "function" &&
      typeof AbortSignal === "function",
      "AbortController/AbortSignal exist");
{
    const ac = new AbortController();
    let aborted = false;
    ac.signal.addEventListener("abort", () => { aborted = true; });
    ac.abort("reason");
    check(ac.signal.aborted === true && aborted === true &&
          ac.signal.reason === "reason",
          "AbortController aborts and notifies listeners");
    check(AbortSignal.abort().aborted === true,
          "AbortSignal.abort() creates an aborted signal");
}

check(typeof URL.canParse === "function" &&
      URL.canParse("https://a.b") === true &&
      URL.canParse("not a url") === false, "URL.canParse works");

/* --- ES2027 candidates (stage 4) — Explicit Resource Management --- */

check(typeof Symbol.dispose === "symbol", "ES2027: Symbol.dispose exists");
check(typeof Symbol.asyncDispose === "symbol", "ES2027: Symbol.asyncDispose exists");
check(typeof DisposableStack === "function", "ES2027: DisposableStack exists");
check(typeof AsyncDisposableStack === "function", "ES2027: AsyncDisposableStack exists");
check(typeof SuppressedError === "function", "ES2027: SuppressedError exists");
{
    const se = new SuppressedError(new Error("a"), new Error("b"), "msg");
    check(se.error.message === "a" && se.suppressed.message === "b" &&
          se.message === "msg", "ES2027: SuppressedError captures both errors");
}
/* using syntax is tested via the engine's syntax support; here we verify
 * the runtime plumbing works end-to-end. */
{
    let disposed = false;
    class Res { [Symbol.dispose]() { disposed = true; } }
    {
        using r = new Res();
        check(r instanceof Res, "ES2027: using binds the resource");
    }
    check(disposed === true, "ES2027: using calls [Symbol.dispose] on scope exit");
}
{
    let asyncDisposed = false;
    class AsyncRes { async [Symbol.asyncDispose]() { asyncDisposed = true; } }
    await (async () => {
        await using r = new AsyncRes();
        check(r instanceof AsyncRes, "ES2027: await using binds the resource");
    })();
    check(asyncDisposed === true, "ES2027: await using calls [Symbol.asyncDispose]");
}

/* --- ES2027 candidates (stage 4) — Joint Iteration --- */

check(typeof Iterator !== "undefined" && typeof Iterator.zip === "function",
      "ES2027: Iterator.zip exists");
check(typeof Iterator.zipKeyed === "function",
      "ES2027: Iterator.zipKeyed exists");
check(typeof Iterator.fromAsync === "function",
      "ES2027: Iterator.fromAsync exists");
{
    async function*agen() { yield 1; yield 2; yield 3; }
    const it = Iterator.fromAsync(agen());
    const first = await it.next();
    check(first.value === 1 && first.done === false,
          "ES2027: Iterator.fromAsync yields from async iterable");
    const collected = [];
    for await (const v of it) collected.push(v);
    check(collected.join(",") === "2,3",
          "ES2027: Iterator.fromAsync remaining values collected");
}
{
    /* Iterator.zip — takes a single iterable of iterables */
    const zipped = Iterator.zip([[1, 2, 3], ["a", "b", "c"]]);
    const pairs = Array.from(zipped);
    check(pairs.length === 3 && pairs[0][0] === 1 && pairs[0][1] === "a",
          "ES2027: Iterator.zip pairs elements lazily");
}
{
    /* Iterator.zipKeyed */
    const keyed = Iterator.zipKeyed({ x: [10, 20], y: ["a", "b"] });
    const objs = Array.from(keyed);
    check(objs.length === 2 && objs[0].x === 10 && objs[0].y === "a",
          "ES2027: Iterator.zipKeyed produces keyed objects");
}

check(typeof Atomics !== "undefined" && typeof Atomics.pause === "function",
      "ES2027: Atomics.pause exists");

/* Promise.try is async; give the microtask queue a chance to drain. */
await Promise.resolve();

if (failures === 0) {
    console.log("== ALL PATCH TESTS PASSED ==");
    std.exit(0);
} else {
    console.log("== " + failures + " PATCH TEST(S) FAILED ==");
    std.exit(1);
}
