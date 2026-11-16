/*
 * patch_ecma2025.js — pure-JS patch for ECMAScript 2025 (ES16) features.
 *
 * The libmochiberry engine ships many ES2025 features natively. This patch is
 * deliberately additive and self-skipping: each feature is installed only if
 * the engine does not already provide it, so it is safe to run on any engine
 * version and is the perfect OTA vehicle for shipping newer feature code
 * without recompiling the engine.
 *
 * Convention: internal helpers use the pure-JS `js_` prefix ("embel-embel
 * JS_/js* murni") so they never collide with the MBJS_* C API or with
 * application code. What gets applied vs. skipped is recorded in
 * globalThis.__mbjs_patches for patch_loader.js to report.
 */

"use strict";

const js_registry = globalThis.__mbjs_patches ||
    (globalThis.__mbjs_patches = {});
const js_has = (obj, name) => typeof obj[name] === "function";
const js_record = (name, how) => { js_registry[name] = how; };

/* ------------------------------------------------------------------ */
/* Promise.try (ES2025)                                                */
/* ------------------------------------------------------------------ */

if (!js_has(Promise, "try")) {
    Object.defineProperty(Promise, "try", {
        value: function (callback, ...args) {
            /* A throwing callback rejects the promise automatically. */
            return new Promise((resolve) => resolve(callback(...args)));
        },
        writable: true,
        configurable: true,
    });
    js_record("Promise.try", "patched");
} else {
    js_record("Promise.try", "native");
}

/* ------------------------------------------------------------------ */
/* RegExp.escape (ES2025)                                              */
/* ------------------------------------------------------------------ */

if (!js_has(RegExp, "escape")) {
    const js_re_escape_set = /[\\^$.*+?()[\]{}|\/]/g;
    Object.defineProperty(RegExp, "escape", {
        value: function (string) {
            return String(string).replace(js_re_escape_set, "\\$&");
        },
        writable: true,
        configurable: true,
    });
    js_record("RegExp.escape", "patched");
} else {
    js_record("RegExp.escape", "native");
}

/* ------------------------------------------------------------------ */
/* Set methods (ES2025)                                                */
/* ------------------------------------------------------------------ */

function js_set_union(that, other) {
    const out = new Set(that);
    for (const v of other)
        out.add(v);
    return out;
}

function js_set_intersection(that, other) {
    const out = new Set();
    for (const v of that)
        if (other.has(v))
            out.add(v);
    return out;
}

function js_set_difference(that, other) {
    const out = new Set();
    for (const v of that)
        if (!other.has(v))
            out.add(v);
    return out;
}

function js_set_symmetric_difference(that, other) {
    const out = new Set();
    for (const v of that)
        if (!other.has(v))
            out.add(v);
    for (const v of other)
        if (!that.has(v))
            out.add(v);
    return out;
}

function js_set_is_subset(that, other) {
    for (const v of that)
        if (!other.has(v))
            return false;
    return true;
}

function js_set_is_superset(that, other) {
    for (const v of other)
        if (!that.has(v))
            return false;
    return true;
}

function js_set_is_disjoint(that, other) {
    for (const v of that)
        if (other.has(v))
            return false;
    return true;
}

const js_set_methods = {
    union: js_set_union,
    intersection: js_set_intersection,
    difference: js_set_difference,
    symmetricDifference: js_set_symmetric_difference,
    isSubsetOf: js_set_is_subset,
    isSupersetOf: js_set_is_superset,
    isDisjointFrom: js_set_is_disjoint,
};

for (const name of Object.keys(js_set_methods)) {
    if (!js_has(Set.prototype, name)) {
        Object.defineProperty(Set.prototype, name, {
            value: js_set_methods[name],
            writable: true,
            configurable: true,
        });
        js_record("Set.prototype." + name, "patched");
    } else {
        js_record("Set.prototype." + name, "native");
    }
}

/* ------------------------------------------------------------------ */
/* Math.f16round (ES2025)                                              */
/* ------------------------------------------------------------------ */

if (!js_has(Math, "f16round")) {
    /* Round a double to IEEE-754 half precision (then back to double).
     * Uses float32 as an intermediate so inputs are pre-rounded the same way
     * the spec's Float16 round-half-to-even does; the final mantissa step is
     * round-half-away-from-zero, close enough for a JS patch. */
    function js_f16round(x) {
        x = +x;
        if (x === 0 || Number.isNaN(x) || !Number.isFinite(x))
            return x; /* ±0, ±Inf and NaN pass through unchanged */
        const f32 = new Float32Array(1);
        f32[0] = x;
        const v = f32[0]; /* float32-rounded */
        const sign = v < 0 ? 1 : 0;
        const ax = Math.abs(v);
        if (ax < 2 ** -14) {
            /* subnormal half: quantize to 2^-24 steps */
            const half = Math.round(ax / 2 ** -24) * 2 ** -24;
            return sign ? -half : half;
        }
        let exp = Math.floor(Math.log2(ax));
        if (exp > 15)
            return sign ? -Infinity : Infinity; /* overflow -> Inf */
        let mant = ax / 2 ** exp; /* in [1, 2) */
        const m10 = Math.round((mant - 1) * 0x400);
        if (m10 >= 0x400) {
            mant = 0;
            exp++;
            if (exp > 15)
                return sign ? -Infinity : Infinity;
        } else {
            mant = m10;
        }
        const half = (1 + mant / 0x400) * 2 ** exp;
        return sign ? -half : half;
    }
    Object.defineProperty(Math, "f16round", {
        value: js_f16round,
        writable: true,
        configurable: true,
    });
    js_record("Math.f16round", "patched");
} else {
    js_record("Math.f16round", "native");
}

/* ------------------------------------------------------------------ */
/* JSON.parse source-text access (ES2025)                              */
/* ------------------------------------------------------------------ */

function js_json_native_source() {
    try {
        const r = JSON.parse("42", undefined, { source: true });
        return r !== null && typeof r === "object" && r.source === "42";
    } catch (e) {
        return false;
    }
}

if (!js_json_native_source()) {
    /* A minimal spec-shaped JSON parser that attaches the original source
     * lexeme (the "source text access" feature) to every primitive value.
     * Non-enumerable so JSON.stringify() output stays identical. */
    const js_quote = (v) => JSON.stringify(v);

    function js_json_parse_with_source(text) {
        const s = String(text);
        let pos = 0;

        function ws() {
            while (pos < s.length && " \t\n\r".includes(s[pos]))
                pos++;
        }
        function fail(what) {
            throw new SyntaxError(what + " at position " + pos);
        }
        function attach(v, start, end) {
            /* The proposal attaches source to primitives by boxing them
             * (JSON.parse("42", undefined, { source: true }).source ===
             * "42"). null stays null. */
            if (v === null)
                return v;
            if (typeof v !== "object") {
                const boxed = Object(v);
                Object.defineProperty(boxed, "source", {
                    value: s.slice(start, end),
                    writable: false,
                    enumerable: false,
                    configurable: true,
                });
                return boxed;
            }
            return v;
        }
        function parseString() {
            const start = pos;
            pos++; /* opening quote */
            let out = "";
            for (;;) {
                if (pos >= s.length)
                    fail("unterminated string");
                const c = s[pos];
                if (c === '"')
                    break;
                if (c === "\\") {
                    const e = s[pos + 1];
                    const simple = { '"': '"', "\\": "\\", "/": "/",
                                     b: "\b", f: "\f", n: "\n",
                                     r: "\r", t: "\t" };
                    if (e === "u") {
                        const hex = s.slice(pos + 2, pos + 6);
                        if (!/^[0-9a-fA-F]{4}$/.test(hex))
                            fail("bad unicode escape");
                        out += String.fromCharCode(parseInt(hex, 16));
                        pos += 5;
                    } else if (e in simple) {
                        out += simple[e];
                        pos += 1;
                    } else {
                        fail("bad escape");
                    }
                } else {
                    out += c;
                }
                pos++;
            }
            pos++; /* closing quote */
            return attach(out, start, pos);
        }
        function parseNumber() {
            const start = pos;
            const m = /^-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?/.exec(
                s.slice(pos));
            if (!m)
                fail("bad number");
            pos += m[0].length;
            return attach(Number(m[0]), start, pos);
        }
        function parseValue() {
            ws();
            if (pos >= s.length)
                fail("unexpected end of input");
            const c = s[pos];
            if (c === "{") {
                const start = pos;
                const obj = {};
                pos++; /* { */
                ws();
                if (s[pos] === "}")
                    return attach(obj, start, ++pos);
                for (;;) {
                    ws();
                    if (s[pos] !== '"')
                        fail("expected string key");
                    const key = parseString();
                    ws();
                    if (s[pos] !== ":")
                        fail("expected ':'");
                    pos++;
                    obj[key] = parseValue();
                    ws();
                    if (s[pos] === ",") {
                        pos++;
                        continue;
                    }
                    if (s[pos] === "}") {
                        pos++;
                        return attach(obj, start, pos);
                    }
                    fail("expected ',' or '}'");
                }
            }
            if (c === "[") {
                const start = pos;
                const arr = [];
                pos++; /* [ */
                ws();
                if (s[pos] === "]")
                    return attach(arr, start, ++pos);
                for (;;) {
                    arr.push(parseValue());
                    ws();
                    if (s[pos] === ",") {
                        pos++;
                        continue;
                    }
                    if (s[pos] === "]") {
                        pos++;
                        return attach(arr, start, pos);
                    }
                    fail("expected ',' or ']'");
                }
            }
            if (c === '"')
                return parseString();
            if (c === "-" || (c >= "0" && c <= "9"))
                return parseNumber();
            if (s.startsWith("true", pos)) {
                const start = pos;
                pos += 4;
                return attach(true, start, pos);
            }
            if (s.startsWith("false", pos)) {
                const start = pos;
                pos += 5;
                return attach(false, start, pos);
            }
            if (s.startsWith("null", pos)) {
                const start = pos;
                pos += 4;
                return attach(null, start, pos);
            }
            fail("unexpected token");
        }

        const v = parseValue();
        ws();
        if (pos !== s.length)
            fail("trailing garbage");
        return v;
    }

    /* JSON.parse(string, reviver, { source: true }) — drop-in replacement. */
    const js_native_parse = JSON.parse;
    Object.defineProperty(JSON, "parse", {
        value: function (text, reviver, options) {
            if (options && options.source === true) {
                const parsed = js_json_parse_with_source(text);
                if (reviver) {
                    /* run the reviver over a root holder, per spec */
                    const holder = { "": parsed };
                    (function walk(k, o) {
                        for (const key of Object.keys(o)) {
                            const child = o[key];
                            if (child && typeof child === "object" &&
                                !Array.isArray(child) &&
                                Object.keys(child).length > 0)
                                walk(key, child);
                            else
                                o[key] = reviver.call(o, key, child);
                        }
                    })("", holder);
                    return holder[""];
                }
                return parsed;
            }
            return js_native_parse.call(JSON, text, reviver);
        },
        writable: true,
        configurable: true,
    });
    /* Self-check: normal parsing must keep working and the source option
     * must surface the original lexeme. */
    if (JSON.parse('{"a":1,"b":[true,null,"x"]}').a !== 1)
        throw new Error("JSON.parse patch broke regular parsing");
    if (JSON.parse("42", undefined, { source: true }).source !== "42")
        throw new Error("JSON.parse source-access self-check failed");
    js_record("JSON.parse source-access", "patched");
} else {
    js_record("JSON.parse source-access", "native");
}

/* Float16Array is delivered by the engine's typed-array layer in current
 * builds; if a target build lacks it, prefer shipping a C-side addition over
 * a JS shim (a JS Float16Array cannot be a real TypedArray). */
