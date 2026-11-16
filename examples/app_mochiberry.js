/*
 * app_mochiberry.js — sample JavaScript application for the libmochiberry
 * "simple machine" (examples/embed_libmochiberry.c).
 *
 * Exercises a mix of engine features: closures, Set/Map, typed arrays,
 * iteration, JSON, base64/hex helpers (when present) and async/await.
 */

"use strict";

const started = Date.now();

function fib(n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

function main() {
    console.log("app: hello from libmochiberry");

    // Pure computation.
    const f = fib(20);
    console.log("app: fib(20) =", f);

    // Collections + iteration.
    const set = new Set([3, 1, 4, 1, 5, 9, 2, 6]);
    console.log("app: set size =", set.size);
    const sorted = [...set].sort((a, b) => a - b);
    console.log("app: sorted set =", sorted.join(","));

    // Typed arrays + binary helpers.
    const bytes = new Uint8Array([0xde, 0xad, 0xbe, 0xef]);
    console.log("app: hex =", bytes.toHex ? bytes.toHex() : "(toHex unavailable)");
    const b64 = bytes.toBase64 ? bytes.toBase64() : "(toBase64 unavailable)";
    console.log("app: base64 =", b64);

    // JSON round trip.
    const obj = { name: "mochiberry", level: 42, tags: ["js", "embedded", "ota"] };
    const revived = JSON.parse(JSON.stringify(obj));
    console.log("app: json ok =", revived.level === 42 && revived.tags.length === 3);

    // Generators.
    function* countdown(n) {
        while (n > 0) yield n--;
    }
    console.log("app: countdown =", [...countdown(3)].join(","));

    return f;
}

const result = main();
console.log("app: main returned", result, "after", Date.now() - started, "ms");

// Exercise the microtask queue.
Promise.resolve("async ok").then((msg) => {
    console.log("app:", msg);
});
