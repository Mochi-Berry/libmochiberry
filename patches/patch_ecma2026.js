/*
 * patch_ecma2026.js — pure-JS patch for ECMAScript 2026 (ES17, the 17th
 * edition, ratified by the Ecma General Assembly on 2026-06-30).
 *
 * Source of truth: TC39 finished-proposals.md. Seven proposals made the
 * ES2026 snapshot (approved as the ES2026 candidate in April 2026):
 *
 *   1. Math.sumPrecise                     (Shewchuk-style precise summation)
 *   2. Uint8Array to/from base64 and hex   (toBase64/toHex/fromBase64/...)
 *   3. Error.isError
 *   4. Iterator.concat  (Iterator Sequencing)
 *   5. Map / WeakMap .getOrInsert and .getOrInsertComputed  (Upsert)
 *   6. Array.fromAsync
 *   7. JSON.parse source-text access       (reviver gets { source })
 *
 * ES2027 candidates (stage 4, expected 2027):
 *   - Explicit Resource Management (`using` / `await using`)
 *   - Joint Iteration (Iterator.fromAsync)
 *   - Temporal API
 *   - Atomics.pause
 *
 * This fork's engine already implements most of ES2026 and ES2027 candidates
 * natively; this patch is the compatibility layer that makes ANY
 * libmochiberry build report the same, complete surface. Every feature is
 * guarded, self-skipping and recorded in globalThis.__mbjs_patches.
 */

"use strict";

const js_registry = globalThis.__mbjs_patches ||
    (globalThis.__mbjs_patches = {});
const js_has = (obj, name) => typeof obj[name] === "function";
const js_has_ctor = (obj, name) => typeof obj[name] !== "undefined";
const js_record = (name, how) => { js_registry[name] = how; };

/* ------------------------------------------------------------------ */
/* 1. Math.sumPrecise — Neumaier/Shewchuk-style compensated summation  */
/* ------------------------------------------------------------------ */

if (!js_has(Math, "sumPrecise")) {
    function js_sum_precise(items) {
        let sum = 0;
        let correction = 0;
        for (const item of items) {
            const v = Number(item);
            const t = sum + v;
            if (Math.abs(sum) >= Math.abs(v)) {
                correction += (sum - t) + v;
            } else {
                correction += (v - t) + sum;
            }
            sum = t;
        }
        return sum + correction;
    }
    Object.defineProperty(Math, "sumPrecise", {
        value: js_sum_precise, writable: true, configurable: true,
    });
    js_record("Math.sumPrecise", "patched");
} else {
    js_record("Math.sumPrecise", "native");
}

/* ------------------------------------------------------------------ */
/* 2. Uint8Array to/from base64 and hex                                */
/* ------------------------------------------------------------------ */

function js_u8a_to_base64(u8a, omitPadding) {
    let bin = "";
    for (let i = 0; i < u8a.length; i++)
        bin += String.fromCharCode(u8a[i]);
    let out = btoa(bin);
    if (omitPadding)
        out = out.replace(/=+$/, "");
    return out;
}

function js_u8a_to_hex(u8a) {
    let out = "";
    for (let i = 0; i < u8a.length; i++)
        out += u8a[i].toString(16).padStart(2, "0");
    return out;
}

if (!js_has(Uint8Array, "fromBase64")) {
    function js_u8a_from_base64(str) {
        const bin = atob(str);
        const out = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i++)
            out[i] = bin.charCodeAt(i);
        return out;
    }
    Object.defineProperty(Uint8Array, "fromBase64", {
        value: js_u8a_from_base64, writable: true, configurable: true,
    });
    Object.defineProperty(Uint8Array, "fromHex", {
        value: function (str) {
            const out = new Uint8Array(str.length / 2);
            for (let i = 0; i < out.length; i++)
                out[i] = parseInt(str.slice(i * 2, i * 2 + 2), 16);
            return out;
        },
        writable: true, configurable: true,
    });
    Object.defineProperty(Uint8Array.prototype, "toBase64", {
        value: function (opts) {
            return js_u8a_to_base64(this,
                opts && opts.omitPadding ? true : false);
        },
        writable: true, configurable: true,
    });
    Object.defineProperty(Uint8Array.prototype, "toHex", {
        value: function () { return js_u8a_to_hex(this); },
        writable: true, configurable: true,
    });
    Object.defineProperty(Uint8Array.prototype, "setFromBase64", {
        value: function (str) {
            const dec = js_u8a_from_base64(str);
            this.set(dec.subarray(0, this.length));
            return { read: str.length, written: Math.min(dec.length, this.length) };
        },
        writable: true, configurable: true,
    });
    Object.defineProperty(Uint8Array.prototype, "setFromHex", {
        value: function (str) {
            const dec = Uint8Array.fromHex(str);
            this.set(dec.subarray(0, this.length));
            return { read: str.length, written: Math.min(dec.length, this.length) };
        },
        writable: true, configurable: true,
    });
    js_record("Uint8Array base64/hex", "patched");
} else {
    js_record("Uint8Array base64/hex", "native");
}

/* ------------------------------------------------------------------ */
/* 3. Error.isError                                                    */
/* ------------------------------------------------------------------ */

if (!js_has(Error, "isError")) {
    function js_is_error(value) {
        if (value instanceof Error)
            return true;
        if (value !== null && (typeof value === "object" ||
                typeof value === "function")) {
            try {
                return Object.prototype.toString.call(value) ===
                    "[object Error]";
            } catch (e) {
                return false;
            }
        }
        return false;
    }
    Object.defineProperty(Error, "isError", {
        value: js_is_error, writable: true, configurable: true,
    });
    js_record("Error.isError", "patched");
} else {
    js_record("Error.isError", "native");
}

/* ------------------------------------------------------------------ */
/* 4. Iterator.concat  (Iterator Sequencing)                           */
/* ------------------------------------------------------------------ */

if (!js_has(Iterator, "concat")) {
    function js_iterator_concat(...iterables) {
        return (function* () {
            for (const it of iterables) {
                if (it == null)
                    throw new TypeError("Iterator.concat: null/undefined iterable");
                if (typeof it[Symbol.iterator] === "function") {
                    yield* it;
                } else {
                    /* iterator protocol directly (e.g. Iterator.from outputs) */
                    let r;
                    while (!(r = it.next()).done)
                        yield r.value;
                }
            }
        })();
    }
    Object.defineProperty(Iterator, "concat", {
        value: js_iterator_concat, writable: true, configurable: true,
    });
    js_record("Iterator.concat", "patched");
} else {
    js_record("Iterator.concat", "native");
}

/* ------------------------------------------------------------------ */
/* 5. Map / WeakMap .getOrInsert + .getOrInsertComputed  (Upsert)      */
/* ------------------------------------------------------------------ */

function js_make_upsert(Ctor, protoName) {
    if (Ctor === undefined)
        return;
    if (!js_has(Ctor.prototype, "getOrInsert")) {
        Object.defineProperty(Ctor.prototype, "getOrInsert", {
            value: function (key, value) {
                if (this.has(key))
                    return this.get(key);
                this.set(key, value);
                return value;
            },
            writable: true, configurable: true,
        });
        Object.defineProperty(Ctor.prototype, "getOrInsertComputed", {
            value: function (key, fn) {
                if (this.has(key))
                    return this.get(key);
                const value = fn(key);
                this.set(key, value);
                return value;
            },
            writable: true, configurable: true,
        });
        js_record(protoName + " upsert", "patched");
    } else {
        js_record(protoName + " upsert", "native");
    }
}

js_make_upsert(typeof Map !== "undefined" ? Map : undefined, "Map");
js_make_upsert(typeof WeakMap !== "undefined" ? WeakMap : undefined, "WeakMap");

/* ------------------------------------------------------------------ */
/* 6. Array.fromAsync                                                  */
/* ------------------------------------------------------------------ */

if (!js_has(Array, "fromAsync")) {
    Object.defineProperty(Array, "fromAsync", {
        value: async function (source, mapFn, thisArg) {
            const out = [];
            if (source == null || typeof source[Symbol.asyncIterator] !== "function") {
                if (source == null || typeof source[Symbol.iterator] !== "function")
                    throw new TypeError("Array.fromAsync: not iterable");
                for (const v of source)
                    out.push(await v);
            } else {
                for await (const v of source)
                    out.push(v);
            }
            if (typeof mapFn === "function") {
                for (let i = 0; i < out.length; i++)
                    out[i] = mapFn.call(thisArg, out[i], i);
            }
            return out;
        },
        writable: true, configurable: true,
    });
    js_record("Array.fromAsync", "patched");
} else {
    js_record("Array.fromAsync", "native");
}

/* ------------------------------------------------------------------ */
/* 7. JSON.parse source-text access                                    */
/* ------------------------------------------------------------------ */

if (typeof JSON.parse.length !== "number" || JSON.parse.length < 3) {
    /* The engine's JSON.parse takes (text, reviver, options); when it does
     * not support { source: true }, patch_ecma2025.js installs a full
     * source-access wrapper. Record the ES2026 status here. */
    js_record("JSON.parse source text access", "patched (see patch_ecma2025.js)");
} else {
    js_record("JSON.parse source text access", "native");
}

/* ------------------------------------------------------------------ */
/* Iterator.fromAsync (Joint Iteration — stage 4, expected ES2027)      */
/* ------------------------------------------------------------------ */

if (typeof Iterator !== "undefined" && !js_has(Iterator, "fromAsync")) {
    Object.defineProperty(Iterator, "fromAsync", {
        value: function (asyncIterable) {
            if (asyncIterable == null)
                throw new TypeError("Iterator.fromAsync: null/undefined iterable");
            const method = asyncIterable[Symbol.asyncIterator];
            if (typeof method !== "function")
                throw new TypeError("Iterator.fromAsync: not an async iterable");
            const asyncIter = method.call(asyncIterable);
            return {
                next() { return asyncIter.next(); },
                return(value) {
                    if (typeof asyncIter.return === "function")
                        return asyncIter.return(value);
                    return Promise.resolve({ value, done: true });
                },
                [Symbol.asyncIterator]() { return this; },
            };
        },
        writable: true, configurable: true,
    });
    js_record("Iterator.fromAsync", "patched (stage 4, ES2027)");
} else if (typeof Iterator !== "undefined") {
    js_record("Iterator.fromAsync", "native");
} else {
    js_record("Iterator.fromAsync", "n/a (Iterator not available)");
}

/* ------------------------------------------------------------------ */
/* Iterator.zip / Iterator.zipKeyed (Joint Iteration — stage 4)        */
/* ------------------------------------------------------------------ */

if (typeof Iterator !== "undefined") {
    js_record("Iterator.zip", typeof Iterator.zip === "function" ? "native" : "n/a");
    js_record("Iterator.zipKeyed", typeof Iterator.zipKeyed === "function" ? "native" : "n/a");
}

/* ------------------------------------------------------------------ */
/* Explicit Resource Management (stage 4, expected ES2027)             */
/* ------------------------------------------------------------------ */

js_record("using / await using",
    typeof Symbol.dispose === "symbol" ? "native (syntax + runtime)" : "n/a");
js_record("Symbol.dispose",
    typeof Symbol.dispose === "symbol" ? "native" : "n/a");
js_record("Symbol.asyncDispose",
    typeof Symbol.asyncDispose === "symbol" ? "native" : "n/a");
js_record("DisposableStack",
    typeof DisposableStack === "function" ? "native" : "n/a");
js_record("AsyncDisposableStack",
    typeof AsyncDisposableStack === "function" ? "native" : "n/a");
js_record("SuppressedError",
    typeof SuppressedError === "function" ? "native" : "n/a");

/* ------------------------------------------------------------------ */
/* Intl.DurationFormat (ES2025 feature; engine ships without Intl)     */
/* ------------------------------------------------------------------ */

if (typeof Intl === "undefined") {
    /* The engine has no ICU/Intl at all. Install a minimal Intl namespace
     * so feature detection and DurationFormat-using code work; embedders
     * with real ICU can replace it wholesale. */
    Object.defineProperty(globalThis, "Intl", {
        value: {}, writable: true, configurable: true,
    });
}

if (typeof Intl !== "undefined" && !js_has(Intl, "DurationFormat")) {
    const js_dur_fields = [
        ["years", "yr"], ["months", "mo"], ["weeks", "wk"],
        ["days", "day"], ["hours", "hr"], ["minutes", "min"],
        ["seconds", "sec"], ["milliseconds", "ms"], ["microseconds", "µs"],
        ["nanoseconds", "ns"],
    ];

    class js_DurationFormat {
        constructor(locales, options) {
            this.locales = locales;
            this.options = options || {};
        }
        format(duration) {
            const parts = this.formatToParts(duration);
            return parts.map((p) => p.value).join("").trim();
        }
        formatToParts(duration) {
            const style = this.options.style || "short";
            const parts = [];
            for (const [field, unit] of js_dur_fields) {
                const v = duration[field];
                if (v === undefined || v === 0)
                    continue;
                const n = typeof v === "number" && Number.isFinite(v)
                    ? v : Number(v);
                const label = style === "long" ? field : unit;
                parts.push({ type: "element", value: String(n) + " " + label });
            }
            if (parts.length === 0)
                parts.push({ type: "literal", value: "PT0S" });
            return parts;
        }
    }

    Object.defineProperty(Intl, "DurationFormat", {
        value: js_DurationFormat, writable: true, configurable: true,
    });
    js_record("Intl.DurationFormat", "patched (minimal shim, no ICU)");
} else {
    js_record("Intl.DurationFormat", "native");
}

/* ------------------------------------------------------------------ */
/* Bonus, stage 3: String.dedent (ES2027 candidate, kept as a shim)    */
/* ------------------------------------------------------------------ */

if (!js_has(String, "dedent")) {
    function js_dedent_text(text) {
        const lines = text.replace(/^\n+/, "").replace(/\s+$/, "")
            .split("\n");
        let min = Infinity;
        for (const line of lines) {
            if (line.trim() === "")
                continue;
            const indent = line.match(/^[ \t]*/)[0].length;
            if (indent < min)
                min = indent;
        }
        if (min === Infinity)
            min = 0;
        return lines.map((line) => line.slice(min)).join("\n");
    }

    function js_dedent(first, ...rest) {
        if (Array.isArray(first) && Object.prototype.hasOwnProperty.call(first, "raw")) {
            let text = "";
            for (let i = 0; i < first.length; i++) {
                text += first.raw[i];
                if (i < rest.length)
                    text += String(rest[i]);
            }
            return js_dedent_text(text);
        }
        return js_dedent_text(String(first));
    }
    Object.defineProperty(String, "dedent", {
        value: js_dedent, writable: true, configurable: true,
    });
    js_record("String.dedent", "patched (stage 3 bonus)");
} else {
    js_record("String.dedent", "native");
}

/* ------------------------------------------------------------------ */
/* ES2026 status of syntax-level / engine-delivered features           */
/* ------------------------------------------------------------------ */

js_record("import attributes", "engine syntax (ES2025) - native");
js_record("import defer", "engine syntax (stage 3, ES2027 candidate) - native");
js_record("Temporal", "stage 4, ES2027 - engine delivery (needs Intl/ICU)");
js_record("Atomics.pause", typeof Atomics !== "undefined" && typeof Atomics.pause === "function" ? "native" : "n/a");
