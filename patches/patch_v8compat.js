/*
 * patch_v8compat.js — V8 / Node.js runtime-compatibility layer for
 * libmochiberry.
 *
 * V8's *syntax* is plain ECMA-262, which the engine already implements
 * (plus the ECMA2025/2026 patches). What applications written for V8/Node
 * usually trip on is the *runtime* surface: structuredClone, TextEncoder,
 * timers, URL and crypto. This patch installs only what the engine lacks,
 * in pure JS, so V8-targeting code runs unmodified on MBJS.
 *
 * Notes:
 *   - Timers are backed by the engine's mbjs:os module (exposed by
 *     patch_loader.js as globalThis.__mbjs_os).
 *   - crypto.getRandomValues is a Math.random()-based shim: fine for ids and
 *     tests, NOT cryptographically secure. Bare-metal boards should install
 *     their hardware RNG over this slot.
 *   - fetch() has no network layer in the engine core; a throwing stub is
 *     installed so feature-detection behaves, and embedders can replace it.
 */

"use strict";

const js_registry = globalThis.__mbjs_patches ||
    (globalThis.__mbjs_patches = {});
const js_os = globalThis.__mbjs_os || null;
const js_record = (name, how) => { js_registry[name] = how; };
const js_has = (obj, name) => typeof obj[name] !== "undefined";

/* ------------------------------------------------------------------ */
/* structuredClone(value)                                              */
/* ------------------------------------------------------------------ */

if (!js_has(globalThis, "structuredClone")) {
    function js_clone_value(value, seen) {
        if (value === null || typeof value !== "object")
            return value;
        if (typeof value === "object" || typeof value === "function") {
            /* boxed primitives */
            if (value instanceof Number) return Number(value.valueOf());
            if (value instanceof String) return String(value.valueOf());
            if (value instanceof Boolean) return Boolean(value.valueOf());
            if (value instanceof BigInt) return BigInt(value.valueOf());
        }
        if (seen.has(value))
            return seen.get(value);
        let out;
        if (Array.isArray(value)) {
            out = [];
            seen.set(value, out);
            for (const v of value) out.push(js_clone_value(v, seen));
        } else if (value instanceof Date) {
            out = new Date(value.getTime());
            seen.set(value, out);
        } else if (value instanceof RegExp) {
            out = new RegExp(value.source, value.flags);
            seen.set(value, out);
        } else if (value instanceof Map) {
            out = new Map();
            seen.set(value, out);
            for (const [k, v] of value)
                out.set(js_clone_value(k, seen), js_clone_value(v, seen));
        } else if (value instanceof Set) {
            out = new Set();
            seen.set(value, out);
            for (const v of value) out.add(js_clone_value(v, seen));
        } else if (ArrayBuffer.isView(value)) {
            out = new value.constructor(value);
            seen.set(value, out);
        } else if (value instanceof ArrayBuffer) {
            out = value.slice(0);
            seen.set(value, out);
        } else {
            out = {};
            seen.set(value, out);
            for (const key of Object.keys(value))
                out[key] = js_clone_value(value[key], seen);
        }
        return out;
    }

    Object.defineProperty(globalThis, "structuredClone", {
        value: function (value) {
            return js_clone_value(value, new WeakMap());
        },
        writable: true,
        configurable: true,
    });
    js_record("structuredClone", "patched");
} else {
    js_record("structuredClone", "native");
}

/* ------------------------------------------------------------------ */
/* TextEncoder / TextDecoder (UTF-8)                                   */
/* ------------------------------------------------------------------ */

if (!js_has(globalThis, "TextEncoder")) {
    class js_TextEncoder {
        get encoding() { return "utf-8"; }
        encode(input) {
            input = input === undefined ? "" : String(input);
            const bytes = [];
            for (const ch of input) {
                let cp = ch.codePointAt(0);
                if (cp < 0x80) {
                    bytes.push(cp);
                } else if (cp < 0x800) {
                    bytes.push(0xc0 | (cp >> 6), 0x80 | (cp & 0x3f));
                } else if (cp < 0x10000) {
                    bytes.push(0xe0 | (cp >> 12),
                               0x80 | ((cp >> 6) & 0x3f),
                               0x80 | (cp & 0x3f));
                } else {
                    bytes.push(0xf0 | (cp >> 18),
                               0x80 | ((cp >> 12) & 0x3f),
                               0x80 | ((cp >> 6) & 0x3f),
                               0x80 | (cp & 0x3f));
                }
            }
            return Uint8Array.from(bytes);
        }
        encodeInto(input, dest) {
            const bytes = this.encode(input);
            const n = Math.min(bytes.length, dest.length);
            dest.set(bytes.subarray(0, n));
            return { read: input.length, written: n };
        }
    }
    Object.defineProperty(globalThis, "TextEncoder", {
        value: js_TextEncoder, writable: true, configurable: true,
    });
    js_record("TextEncoder", "patched");
} else {
    js_record("TextEncoder", "native");
}

if (!js_has(globalThis, "TextDecoder")) {
    class js_TextDecoder {
        constructor(label, options) {
            this.encoding = "utf-8";
            this.fatal = !!(options && options.fatal);
            this.ignoreBOM = !!(options && options.ignoreBOM);
        }
        decode(input) {
            input = input === undefined ? new Uint8Array(0) : input;
            if (!(input instanceof Uint8Array) && ArrayBuffer.isView(input))
                input = new Uint8Array(input.buffer, input.byteOffset,
                                       input.byteLength);
            if (input instanceof ArrayBuffer)
                input = new Uint8Array(input);
            let out = "";
            let i = 0;
            while (i < input.length) {
                const b0 = input[i];
                let cp, len;
                if (b0 < 0x80) { cp = b0; len = 1; }
                else if ((b0 & 0xe0) === 0xc0) { cp = b0 & 0x1f; len = 2; }
                else if ((b0 & 0xf0) === 0xe0) { cp = b0 & 0x0f; len = 3; }
                else if ((b0 & 0xf8) === 0xf0) { cp = b0 & 0x07; len = 4; }
                else { cp = 0xfffd; len = 1; }
                let valid = true;
                if (len > 1 && i + len > input.length) { valid = false; }
                if (valid) {
                    for (let j = 1; j < len; j++) {
                        if ((input[i + j] & 0xc0) !== 0x80) {
                            valid = false;
                            break;
                        }
                        cp = (cp << 6) | (input[i + j] & 0x3f);
                    }
                }
                if (!valid) {
                    cp = 0xfffd;
                    len = 1;
                } else if (cp > 0x10ffff ||
                           (len === 1 && cp > 0x7f) ||
                           (len === 2 && (cp < 0x80 || cp > 0x7ff)) ||
                           (len === 3 && (cp < 0x800 || cp > 0xffff)) ||
                           (len === 4 && (cp < 0x10000 || cp > 0x10ffff))) {
                    /* overlong / out-of-range sequences decode to U+FFFD */
                    cp = 0xfffd;
                    len = 1;
                }
                out += String.fromCodePoint(cp);
                i += len;
            }
            return out;
        }
    }
    Object.defineProperty(globalThis, "TextDecoder", {
        value: js_TextDecoder, writable: true, configurable: true,
    });
    js_record("TextDecoder", "patched");
} else {
    js_record("TextDecoder", "native");
}

/* ------------------------------------------------------------------ */
/* Timers (backed by mbjs:os)                                          */
/* ------------------------------------------------------------------ */

if (!js_has(globalThis, "setTimeout")) {
    if (js_os && typeof js_os.setTimeout === "function") {
        globalThis.setTimeout = (fn, ms, ...args) =>
            js_os.setTimeout(() => { fn(...args); }, ms);
        globalThis.setInterval = (fn, ms, ...args) =>
            js_os.setInterval(() => { fn(...args); }, ms);
        globalThis.clearTimeout = js_os.clearTimeout;
        globalThis.clearInterval = js_os.clearInterval;
        js_record("timers (setTimeout/setInterval)", "patched (mbjs:os)");
    } else {
        /* No OS module: timers cannot run; install throwing stubs so
         * feature detection still sees them. */
        globalThis.setTimeout = () => { throw new Error("setTimeout: no OS timer layer"); };
        globalThis.setInterval = () => { throw new Error("setInterval: no OS timer layer"); };
        globalThis.clearTimeout = () => {};
        globalThis.clearInterval = () => {};
        js_record("timers (setTimeout/setInterval)", "patched (no OS: stubs)");
    }
} else {
    js_record("timers (setTimeout/setInterval)", "native");
}

/* ------------------------------------------------------------------ */
/* crypto (non-secure shim; replace with a hardware RNG on devices)    */
/* ------------------------------------------------------------------ */

if (!js_has(globalThis, "crypto")) {
    const js_crypto = {};
    js_crypto.getRandomValues = function (array) {
        if (!array || !array.BYTES_PER_ELEMENT)
            throw new TypeError("crypto.getRandomValues: expected a typed array");
        const view = new Uint8Array(array.buffer, array.byteOffset,
                                    array.byteLength);
        for (let i = 0; i < view.length; i++)
            view[i] = Math.floor(Math.random() * 256);
        return array;
    };
    js_crypto.randomUUID = function () {
        const b = new Uint8Array(16);
        js_crypto.getRandomValues(b);
        b[6] = (b[6] & 0x0f) | 0x40;
        b[8] = (b[8] & 0x3f) | 0x80;
        const h = Array.from(b, (x) => x.toString(16).padStart(2, "0"));
        return h.slice(0, 4).join("") + "-" + h.slice(4, 6).join("") + "-" +
            h.slice(6, 8).join("") + "-" + h.slice(8, 10).join("") + "-" +
            h.slice(10, 16).join("");
    };
    Object.defineProperty(globalThis, "crypto", {
        value: js_crypto, writable: true, configurable: true,
    });
    js_record("crypto.getRandomValues", "patched (non-secure shim)");
} else {
    js_record("crypto.getRandomValues", "native");
}

/* ------------------------------------------------------------------ */
/* URLSearchParams + URL                                               */
/* ------------------------------------------------------------------ */

if (!js_has(globalThis, "URLSearchParams")) {
    class js_URLSearchParams {
        constructor(init) {
            this._pairs = [];
            if (init === undefined || init === null)
                return;
            if (typeof init === "string") {
                const q = init.replace(/^[?#]/, "");
                if (q === "")
                    return;
                for (const part of q.split("&")) {
                    if (part === "")
                        continue;
                    const eq = part.indexOf("=");
                    if (eq === -1)
                        this._pairs.push([this._dec(part), ""]);
                    else
                        this._pairs.push([this._dec(part.slice(0, eq)),
                                          this._dec(part.slice(eq + 1))]);
                }
            } else if (Array.isArray(init) || init[Symbol.iterator]) {
                for (const pair of init)
                    this.append(pair[0], pair[1]);
            } else if (typeof init === "object") {
                for (const key of Object.keys(init))
                    this.append(key, init[key]);
            }
        }
        _dec(s) {
            try { return decodeURIComponent(s.replace(/\+/g, " ")); }
            catch (e) { return s; }
        }
        _enc(s) {
            return encodeURIComponent(String(s))
                .replace(/[!'()*]/g, (c) => "%" + c.charCodeAt(0).toString(16).toUpperCase());
        }
        append(k, v) { this._pairs.push([String(k), String(v)]); }
        delete(k) {
            this._pairs = this._pairs.filter(([key]) => key !== String(k));
        }
        get(k) {
            const f = this._pairs.find(([key]) => key === String(k));
            return f ? f[1] : null;
        }
        getAll(k) {
            return this._pairs.filter(([key]) => key === String(k))
                .map(([, v]) => v);
        }
        has(k) { return this.get(k) !== null; }
        set(k, v) {
            this.delete(k);
            this.append(k, v);
        }
        sort() {
            this._pairs.sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
        }
        toString() {
            return this._pairs.map(([k, v]) =>
                this._enc(k) + "=" + this._enc(v)).join("&");
        }
        entries() { return this._pairs[Symbol.iterator](); }
        keys() {
            const self = this;
            return (function* () {
                for (const [k] of self._pairs) yield k;
            })();
        }
        values() {
            const self = this;
            return (function* () {
                for (const [, v] of self._pairs) yield v;
            })();
        }
        forEach(fn, thisArg) {
            for (const [k, v] of this._pairs)
                fn.call(thisArg, v, k, this);
        }
        get size() { return this._pairs.length; }
        [Symbol.iterator]() { return this.entries(); }
    }
    Object.defineProperty(globalThis, "URLSearchParams", {
        value: js_URLSearchParams, writable: true, configurable: true,
    });
    js_record("URLSearchParams", "patched");
} else {
    js_record("URLSearchParams", "native");
}

if (!js_has(globalThis, "URL")) {
    class js_URL {
        constructor(url, base) {
            let input = String(url);
            if (base !== undefined && !/^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(input)) {
                const b = new js_URL(String(base));
                input = b.origin + (input.startsWith("/") ? "" : "/") + input;
            }
            const m = /^(?:([a-zA-Z][a-zA-Z0-9+.-]*):)?(?:\/\/(?:([^@/?#]*)@)?(\[[^\]]*\]|[^:/?#]*)(?::(\d*))?)?([^?#]*)(\?[^#]*)?(#.*)?$/
                .exec(input);
            /* A URL must have a scheme+host, or come from a base. */
            if (!m || ((!m[1] || !m[3]) && base === undefined))
                throw new TypeError("Invalid URL: " + input);
            this._scheme = (m[1] || "https:").toLowerCase();
            let auth = m[2] || "";
            if (auth) {
                const at = auth.indexOf(":");
                if (at === -1) { this.username = auth; this.password = ""; }
                else { this.username = auth.slice(0, at); this.password = auth.slice(at + 1); }
            } else {
                this.username = "";
                this.password = "";
            }
            this.hostname = (m[3] || "").toLowerCase();
            this.port = m[4] || "";
            this.pathname = m[5] || "/";
            this.search = m[6] || "";
            this.hash = m[7] || "";
            /* URLSearchParams is installed by the sibling block (or native) */
            this.searchParams = new (globalThis.URLSearchParams)(this.search);
        }
        get href() {
            let s = this._scheme + "//";
            if (this.username || this.password) {
                s += this.username;
                if (this.password) s += ":" + this.password;
                s += "@";
            }
            s += this.hostname;
            if (this.port) s += ":" + this.port;
            s += this.pathname || "/";
            s += this.search;
            s += this.hash;
            return s;
        }
        get protocol() { return this._scheme + ":"; }
        get origin() {
            return this._scheme + "//" + this.hostname +
                (this.port ? ":" + this.port : "");
        }
        get host() {
            return this.hostname + (this.port ? ":" + this.port : "");
        }
        toString() { return this.href; }
        toJSON() { return this.href; }
    }
    Object.defineProperty(globalThis, "URL", {
        value: js_URL, writable: true, configurable: true,
    });
    js_record("URL", "patched");
} else {
    js_record("URL", "native");
}

/* ------------------------------------------------------------------ */
/* AbortController / AbortSignal (V8/Node runtime staple)               */
/* ------------------------------------------------------------------ */

if (!js_has(globalThis, "AbortController")) {
    class js_AbortSignal {
        constructor() {
            this.aborted = false;
            this.reason = undefined;
            this.onabort = null;
            this._listeners = new Set();
        }
        addEventListener(type, fn) {
            if (type === "abort" && typeof fn === "function")
                this._listeners.add(fn);
        }
        removeEventListener(type, fn) {
            if (type === "abort")
                this._listeners.delete(fn);
        }
        dispatchEvent(event) {
            if (event && event.type === "abort" && this.aborted) {
                for (const fn of this._listeners)
                    fn.call(this, event);
                if (typeof this.onabort === "function")
                    this.onabort.call(this, event);
            }
            return true;
        }
        get [Symbol.toStringTag]() { return "AbortSignal"; }
        static abort(reason) {
            const s = new js_AbortSignal();
            s.aborted = true;
            s.reason = reason === undefined ? new Error("Aborted") : reason;
            return s;
        }
        static timeout(ms) {
            const c = new js_AbortController();
            if (typeof setTimeout === "function")
                setTimeout(() => c.abort(new Error("Timeout")), ms);
            return c.signal;
        }
    }
    class js_AbortController {
        constructor() {
            this.signal = new js_AbortSignal();
        }
        abort(reason) {
            if (this.signal.aborted)
                return;
            this.signal.aborted = true;
            this.signal.reason = reason === undefined
                ? new Error("Aborted") : reason;
            this.signal.dispatchEvent({ type: "abort" });
        }
        get [Symbol.toStringTag]() { return "AbortController"; }
    }
    Object.defineProperty(globalThis, "AbortController", {
        value: js_AbortController, writable: true, configurable: true,
    });
    Object.defineProperty(globalThis, "AbortSignal", {
        value: js_AbortSignal, writable: true, configurable: true,
    });
    js_record("AbortController/AbortSignal", "patched");
} else {
    js_record("AbortController/AbortSignal", "native");
}

/* URL.canParse — cheap existence check without throwing. */
if (js_has(globalThis, "URL") && !js_has(URL, "canParse")) {
    Object.defineProperty(URL, "canParse", {
        value: function (url, base) {
            try { new URL(url, base); return true; } catch (e) { return false; }
        },
        writable: true,
        configurable: true,
    });
    js_record("URL.canParse", "patched");
} else {
    js_record("URL.canParse", js_has(globalThis, "URL") ? "native" : "n/a");
}

/* ------------------------------------------------------------------ */
/* fetch(): there is no network layer in the engine core               */
/* ------------------------------------------------------------------ */

if (!js_has(globalThis, "fetch")) {
    Object.defineProperty(globalThis, "fetch", {
        value: function () {
            return Promise.reject(new Error(
                "fetch: no network layer on this platform (libmochiberry); " +
                "the embedder must install one"));
        },
        writable: true,
        configurable: true,
    });
    js_record("fetch", "patched (stub: no network layer)");
} else {
    js_record("fetch", "native");
}
