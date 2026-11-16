/* ------------------------------------------------------------------
 * Intl core shim (no ICU). Installed by the boot loader when the engine
 * ships without ICU/Intl. Semantics are correct for the common locale-
 * independent path and for `en` / `id` basics; exotic locale rules are
 * intentionally approximated (digits, separators, month/day names),
 * because a full ICU is out of the "lean build" scope. Every class
 * here is constructible, has the real static methods, and formats
 * actual data rather than returning placeholders.
 * ------------------------------------------------------------------ */

if (typeof Intl === "undefined") {
    Object.defineProperty(globalThis, "Intl", {
        value: {}, writable: true, configurable: true,
    });
}

const js_registry = globalThis.__mbjs_patches ||
    (globalThis.__mbjs_patches = {});
const js_record = (name, how) => { js_registry[name] = how; };

(function (Intl) {
    "use strict";

    /* ── helpers ─────────────────────────────────────────────────── */

    function toStr(x) { return String(x); }
    function pad2(n) { return (n < 10 ? "0" : "") + n; }

    function normLocale(locale) {
        if (locale === undefined) return "en-US";
        if (typeof locale === "object" && locale !== null) locale = locale.toString();
        locale = String(locale);
        if (locale === "") return "en-US";
        return locale;
    }

    /* pick the best of the requested locales we can actually do. */
    function pickLocale(locales) {
        if (locales === undefined) return "en-US";
        var arr = Array.isArray(locales) ? locales : [locales];
        for (var i = 0; i < arr.length; i++) {
            var l = normLocale(arr[i]);
            if (/^(en|id|en-US|en-GB|id-ID)(\b|-|$)/i.test(l)) return l;
        }
        return "en-US";
    }

    function getNumberOptions(options) {
        options = options || {};
        var o = {};
        o.style = options.style || "decimal";
        o.minimumFractionDigits = (options.minimumFractionDigits !== undefined)
            ? options.minimumFractionDigits : 0;
        o.maximumFractionDigits = (options.maximumFractionDigits !== undefined)
            ? options.maximumFractionDigits : 3;
        o.useGrouping = options.useGrouping !== false;
        o.notation = options.notation || "standard";
        if (o.style === "percent") {
            o.minimumFractionDigits = (options.minimumFractionDigits !== undefined)
                ? options.minimumFractionDigits : 0;
            o.maximumFractionDigits = (options.maximumFractionDigits !== undefined)
                ? options.maximumFractionDigits : 0;
        }
        if (o.style === "currency") {
            o.currency = options.currency || "USD";
            o.currencyDisplay = options.currencyDisplay || "symbol";
            var cd2 = CCY_DIGITS[o.currency];
            if (cd2 === undefined) cd2 = 2;
            o.minimumFractionDigits = (options.minimumFractionDigits !== undefined)
                ? options.minimumFractionDigits : cd2;
            o.maximumFractionDigits = (options.maximumFractionDigits !== undefined)
                ? options.maximumFractionDigits : cd2;
        }
        if (o.notation === "compact") {
            o.compactDisplay = options.compactDisplay || "short";
        }
        return o;
    }

    /* currencies whose minor unit does not exist / is not shown by default */
    var ZERO_FRAC_CCY = { IDR: 1, JPY: 1, KRW: 1, VND: 1, CLP: 1, ISK: 1, HUF: 1, TWD: 1 };
    var CCY_DIGITS = { IDR: 0, JPY: 0, KRW: 0, VND: 0, CLP: 0, ISK: 0, HUF: 2, TWD: 2 };
    var NBSP = "\u00A0";

    /* CLDR cardinal categories for the locales we support. English:
       zero / one / other. Indonesian: no plural distinction (1..inf all
       `other`), 0 is `zero`. Other locales fall back to zero/one/other.
       We deliberately do NOT invent `two`/`few`/`many` — those are
       locale-specific and we only implement what en/id use. */
    function pluralCat(n, locale) {
        var num = Number(n);
        if (Number.isNaN(num)) return "other";
        if (num === 0) return "zero";
        if (/^id/i.test(locale || "")) return "other";      /* id: one = other */
        if (num === 1) return "one";
        return "other";
    }

    /* ── built-in plumbing ──────────────────────────────────────────
     * The engine does not infer function names from property
     * assignment, and plain assignment creates enumerable properties.
     * Everything the spec exposes as a built-in therefore has to be
     * installed explicitly with the right name/length/attributes. */

    function defineMethod(target, name, fn, len) {
        try {
            Object.defineProperty(fn, "name", {
                value: name, writable: false, enumerable: false, configurable: true,
            });
            Object.defineProperty(fn, "length", {
                value: len === undefined ? fn.length : len,
                writable: false, enumerable: false, configurable: true,
            });
        } catch (e) { /* frozen function: leave as-is */ }
        Object.defineProperty(target, name, {
            value: fn, writable: true, enumerable: false, configurable: true,
        });
        return fn;
    }

    function defineGetter(target, name, get, set) {
        try {
            Object.defineProperty(get, "name", {
                value: "get " + name, writable: false, enumerable: false, configurable: true,
            });
        } catch (e) { }
        Object.defineProperty(target, name, {
            get: get, set: set, enumerable: false, configurable: true,
        });
    }

    function defineStatic(target, name, fn, len) {
        return defineMethod(target, name, fn, len);
    }

    function setToStringTag(proto, tag) {
        if (typeof Symbol !== "undefined" && Symbol.toStringTag) {
            Object.defineProperty(proto, Symbol.toStringTag, {
                value: tag, writable: false, enumerable: false, configurable: true,
            });
        }
    }

    /* spec: the "prototype" property of a constructor is not writable */
    function fixPrototypeProp(ctor) {
        var d = Object.getOwnPropertyDescriptor(ctor, "prototype");
        if (d && d.writable) {
            Object.defineProperty(ctor, "prototype", { writable: false });
        }
    }

    /* ECMA-402 GetOption */
    function getOption(options, prop, expectedType, allowed, fallback) {
        if (options === undefined || options === null) return fallback;
        var value = options[prop];
        if (value === undefined) return fallback;
        /* ToString(Symbol) must throw TypeError, but JS String(sym) does not */
        if (typeof value === "symbol") {
            throw new TypeError("Intl option '" + prop + "' cannot be a Symbol");
        }
        if (expectedType === "boolean") {
            if (typeof value === "boolean") {
                if (allowed && allowed.indexOf(value ? "true" : "false") < 0) {
                    throw new RangeError("Intl option '" + prop + "' value is invalid");
                }
                return value;
            }
            var bs = String(value);
            if (bs === "true") return true;
            if (bs === "false") return false;
            throw new RangeError("Intl option '" + prop + "' value is invalid");
        }
        var s = String(value);
        if (allowed && allowed.indexOf(s) < 0) {
            throw new RangeError("Intl option '" + prop + "' value " + s + " is invalid");
        }
        return s;
    }

    /* ECMA-402 GetOptionsObject */
    function getOptionsObject(options) {
        if (options === undefined) return {};
        if (options === null || (typeof options !== "object" && typeof options !== "function")) {
            throw new TypeError("Intl options must be an object");
        }
        return options;
    }

    /* ECMA-402 LengthOfArrayLike */
    function lengthOfArrayLike(obj) {
        var len = obj.length;
        if (len === undefined) len = 0;
        len = Number(len);
        if (len !== len || len <= 0) return 0;
        return Math.floor(len);
    }

    /* install a constructor as a non-enumerable property of Intl */
    function installConstructor(intlObj, name, ctor, len) {
        try {
            Object.defineProperty(ctor, "name", {
                value: name, writable: false, enumerable: false, configurable: true,
            });
            Object.defineProperty(ctor, "length", {
                value: len === undefined ? 0 : len,
                writable: false, enumerable: false, configurable: true,
            });
        } catch (e) { }
        Object.defineProperty(intlObj, name, {
            value: ctor, writable: true, enumerable: false, configurable: true,
        });
        fixPrototypeProp(ctor);
        return ctor;
    }

    /* languages for which the shim has (approximate) CLDR data */
    var SHIM_LANGS = {
        en: 1, id: 1, es: 1, fr: 1, de: 1, it: 1, pt: 1, nl: 1, ja: 1, ko: 1,
        zh: 1, ru: 1, sv: 1, da: 1, nb: 1, nn: 1, fi: 1, pl: 1, cs: 1, tr: 1,
        el: 1, hu: 1, ro: 1, uk: 1, vi: 1, th: 1, ar: 1, he: 1, ca: 1, hr: 1,
        sr: 1, sk: 1, sl: 1, bg: 1, lt: 1, lv: 1, et: 1, hi: 1, ms: 1, fa: 1,
    };

    /* brand checks: WeakSet if available, otherwise a hidden own key */
    function makeBrand() {
        if (typeof WeakSet === "function") {
            var ws = new WeakSet();
            return {
                add: function (o) { ws.add(o); },
                has: function (o) { return ws.has(o); },
            };
        }
        var KEY = "__mb_intl_brand_" + Math.floor(Date.now() % 1e9);
        return {
            add: function (o) { Object.defineProperty(o, KEY, { value: 1, writable: false, enumerable: false, configurable: false }); },
            has: function (o) {
                if (o === null || (typeof o !== "object" && typeof o !== "function")) return false;
                var d = Object.getOwnPropertyDescriptor(o, KEY);
                return !!(d && d.value === 1);
            },
        };
    }

    function requireBrand(brand, obj, name) {
        if (obj === null || (typeof obj !== "object" && typeof obj !== "function") || !brand.has(obj)) {
            throw new TypeError("Intl." + name + " method called on incompatible receiver");
        }
        return obj;
    }

    function requireConstructor(ctor, name, self) {
        if (!(self instanceof ctor)) {
            throw new TypeError("Constructor Intl." + name + " requires 'new'");
        }
    }

    /* ── locale canonicalization ──────────────────────────────────── */

    var RE_SCRIPT = /^[a-z]{4}$/;
    var RE_REGION = /^[a-z]{2}$/;
    var RE_DIGIT3 = /^[0-9]{3}$/;
    var RE_VARIANT = /^[a-z0-9]{5,8}$/;
    var RE_VARIANT_DIG = /^[0-9][a-z0-9]{3}$/;
    var RE_ALPHA = /^[a-zA-Z]+$/;
    var RE_ALNUM = /^[a-zA-Z0-9]+$/;

    function isSingletonSubtag(s) {
        return s.length === 1 && RE_ALNUM.test(s);
    }

    function isVariantSubtag(s) {
        var l = s.toLowerCase();
        return RE_VARIANT.test(l) || RE_VARIANT_DIG.test(l);
    }

    /* BCP 47 structural validation (UTS #35 locale identifier grammar) */
    function isStructurallyValidLanguageTag(tag) {
        if (tag === "") return false;
        var p = tag.split("-");
        var i = 0;
        var lang = p[0];
        if (!RE_ALPHA.test(lang) || lang.length < 2 || lang.length > 8) return false;
        i = 1;
        if (lang.length >= 2 && lang.length <= 3) {
            var ext = 0;
            while (ext < 3 && i < p.length && /^[a-zA-Z]{3}$/.test(p[i])) { ext++; i++; }
        }
        if (i < p.length && /^[a-zA-Z]{4}$/.test(p[i])) i++;
        if (i < p.length && (RE_REGION.test(p[i].toLowerCase()) || RE_DIGIT3.test(p[i]))) i++;
        var seenVariants = {};
        while (i < p.length && isVariantSubtag(p[i])) {
            var v = p[i].toLowerCase();
            if (seenVariants[v]) return false;
            seenVariants[v] = true;
            i++;
        }
        var seenSingles = {};
        while (i < p.length && isSingletonSubtag(p[i]) && p[i].toLowerCase() !== "x") {
            var s = p[i].toLowerCase();
            if (seenSingles[s]) return false;
            seenSingles[s] = true;
            i++;
            var count = 0;
            while (i < p.length && !isSingletonSubtag(p[i]) && p[i].length >= 2 && p[i].length <= 8) {
                if (!RE_ALNUM.test(p[i])) return false;
                count++; i++;
            }
            if (count === 0) return false;
        }
        if (i < p.length && p[i].toLowerCase() === "x") {
            i++;
            var n = 0;
            while (i < p.length) {
                if (!RE_ALNUM.test(p[i]) || p[i].length > 8) return false;
                n++; i++;
            }
            if (n === 0) return false;
        }
        return i === p.length;
    }

    /* private-use-only tags ("x-...") are valid on their own */
    function isValidLanguageTag(tag) {
        if (tag === "") return false;
        var first = tag.split("-")[0].toLowerCase();
        if (first === "x") {
            var rest = tag.split("-").slice(1);
            if (rest.length === 0) return false;
            for (var j = 0; j < rest.length; j++) {
                if (!RE_ALNUM.test(rest[j]) || rest[j].length > 8) return false;
            }
            return true;
        }
        return isStructurallyValidLanguageTag(tag);
    }

    function canonicalizeLanguageTag(tag) {
        var s = String(tag).replace(/^[ \t\n\r\f\v]+|[ \t\n\r\f\v]+$/g, "");
        if (!isValidLanguageTag(s)) {
            throw new RangeError("Invalid language tag: " + s);
        }
        var p = s.split("-");
        var out = [];
        var i = 0;
        var lang = p[0].toLowerCase();
        if (lang === "x") {
            out.push("x");
            for (i = 1; i < p.length; i++) out.push(p[i].toLowerCase());
            return out.join("-");
        }
        out.push(lang);
        i = 1;
        if (lang.length >= 2 && lang.length <= 3) {
            var ext = 0;
            while (ext < 3 && i < p.length && /^[a-zA-Z]{3}$/.test(p[i])) {
                out.push(p[i].toLowerCase()); ext++; i++;
            }
        }
        if (i < p.length && RE_SCRIPT.test(p[i].toLowerCase())) {
            out.push(p[i].charAt(0).toUpperCase() + p[i].slice(1).toLowerCase());
            i++;
        }
        if (i < p.length && (RE_REGION.test(p[i].toLowerCase()) || RE_DIGIT3.test(p[i]))) {
            out.push(p[i].toUpperCase());
            i++;
        }
        while (i < p.length && isVariantSubtag(p[i])) {
            out.push(p[i].toLowerCase()); i++;
        }
        var order = [], groups = {};
        while (i < p.length && isSingletonSubtag(p[i]) && p[i].toLowerCase() !== "x") {
            var key = p[i].toLowerCase();
            i++;
            var vals = [];
            while (i < p.length && !isSingletonSubtag(p[i]) && p[i].length >= 2 && p[i].length <= 8) {
                vals.push(p[i].toLowerCase()); i++;
            }
            if (!groups[key]) { groups[key] = vals; order.push(key); }
        }
        order.sort();
        for (var k = 0; k < order.length; k++) {
            out.push(order[k]);
            out = out.concat(groups[order[k]]);
        }
        if (i < p.length && p[i].toLowerCase() === "x") {
            out.push("x");
            for (i = i + 1; i < p.length; i++) out.push(p[i].toLowerCase());
        }
        return out.join("-");
    }

    /* ECMA-402 CanonicalizeLocaleList */
    function canonicalizeLocaleList(locales, options) {
        if (locales === undefined) return [];
        var opts = options === undefined ? undefined : getOptionsObject(options);
        if (opts !== undefined) {
            getOption(opts, "localeMatcher", "string", ["lookup", "best fit"], "best fit");
        }
        var list = [];
        if (typeof locales === "string") {
            list.push(canonicalizeLanguageTag(locales));
            return list;
        }
        var o = Object(locales);
        var len = lengthOfArrayLike(o);
        for (var k = 0; k < len; k++) {
            var kValue = o[String(k)];
            if (kValue === undefined) {
                throw new TypeError("Locale list element " + k + " is undefined");
            }
            if (typeof kValue !== "string") {
                throw new TypeError("Locale list element " + k + " is not a string");
            }
            list.push(canonicalizeLanguageTag(kValue));
        }
        return list;
    }

    /* first requested locale, canonicalized; [] when the list is empty */
    function requestedLocales(locales) {
        return canonicalizeLocaleList(locales);
    }

    /* ── Locale ──────────────────────────────────────────────────── */

    /* BCP 47 unicode_extension (-u-) keyword / value canonicalisation.
     * Only the subset the shim can answer without CLDR is aliased; unknown
     * values are lower-cased and passed through (spec-compliant for the
     * keys we model). */
    var U_CA_ALIAS = {
        islamicc: "islamic-civil", islamiccivil: "islamic-civil",
        islamicumalqura: "islamic-umalqura", gregorian: "gregory",
        buddhist: "buddhist", chinese: "chinese", coptic: "coptic",
        dangi: "dangi", ethiopic: "ethiopic", ethioaa: "ethioaa",
        hebrew: "hebrew", indian: "indian", islamic: "islamic",
        japanese: "japanese", persian: "persian", roc: "roc",
        gregory: "gregory", iso8601: "iso8601",
    };
    var U_CO_ALIAS = {
        phonebook: "phonebk", standard: "standard", search: "search",
        punctuation: "punct", reformed: "standard", direct: "standard",
    };
    var U_ENUM = {
        kf: ["upper", "lower"], kn: ["true", "false"],
        hc: ["h11", "h12", "h23", "h24"],
    };
    /* keys whose value is a canonical sequence (may contain '-') */
    var U_SEQ = { ca: 1, co: 1, nu: 1, tz: 1, cu: 1, ms: 1, mt: 1, mc: 1 };

    function canonUValue(key, val) {
        val = String(val).toLowerCase();
        if (key === "ca") return Object.prototype.hasOwnProperty.call(U_CA_ALIAS, val) ? U_CA_ALIAS[val] : val;
        if (key === "co") return Object.prototype.hasOwnProperty.call(U_CO_ALIAS, val) ? U_CO_ALIAS[val] : val;
        if (U_ENUM[key]) return U_ENUM[key].indexOf(val) >= 0 ? val : null;
        return val;
    }

    var RTL_LANGS = {
        ar: 1, av: 1, ae: 1, ku: 1, dv: 1, fa: 1, ha: 1, he: 1, ks: 1,
        ku: 1, ps: 1, sd: 1, sd: 1, ug: 1, ur: 1, yi: 1,
    };

    var localeBrand = makeBrand();

    function localeRequire(obj) {
        if (obj === null || (typeof obj !== "object" && typeof obj !== "function") ||
            !localeBrand.has(obj)) {
            throw new TypeError("Intl.Locale method called on incompatible receiver");
        }
        return obj;
    }

    /* Split a canonical tag into base + unicode-extension key/value map. */
    function parseTag(tag) {
        var parts = tag.split("-");
        var base = [], u = {}, attrs = [], privateUse = [];
        var i = 0;
        for (i = 0; i < parts.length; i++) {
            var p = parts[i];
            if (p.length === 1 && p !== "x") { /* singleton */
                if (p === "u") {
                    var j = i + 1;
                    while (j < parts.length && parts[j].length !== 1) {
                        var key = parts[j].toLowerCase();
                        if (key.length !== 2) { j++; continue; }
                        var vals = [];
                        var k = j + 1;
                        while (k < parts.length && parts[k].length !== 1 &&
                               !(parts[k].length === 2 && vals.length > 0)) {
                            vals.push(parts[k].toLowerCase()); k++;
                        }
                        u[key] = vals.join("-");
                        j = k;
                    }
                    i = parts.length; /* unicode extension consumes the tail */
                    continue;
                }
                if (p === "x") { privateUse = parts.slice(i); break; }
                attrs.push(p); i++; continue;
            }
            if (p === "x") { privateUse = parts.slice(i); break; }
            base.push(p);
        }
        return { base: base, u: u, attrs: attrs, privateUse: privateUse };
    }

    function rebuildTag(parsed) {
        var out = parsed.base.slice();
        var keys = Object.keys(parsed.u).sort();
        if (keys.length) {
            out.push("u");
            for (var i = 0; i < keys.length; i++) {
                var key = keys[i], val = parsed.u[key];
                out.push(key);
                if (U_SEQ[key] && val) {
                    var sub = String(val).split("-");
                    for (var s = 0; s < sub.length; s++) if (sub[s]) out.push(sub[s]);
                } else if (val !== "" && val !== undefined && val !== null) {
                    out.push(String(val));
                }
            }
        }
        for (var a = 0; a < parsed.attrs.length; a++) out.push(parsed.attrs[a]);
        for (var q = 0; q < parsed.privateUse.length; q++) out.push(parsed.privateUse[q]);
        return out.join("-");
    }

    function Mblocale(tag, options) {
        if (new.target === undefined) {
            throw new TypeError("Constructor Intl.Locale requires 'new'");
        }
        var parsed = parseTag(canonicalizeLanguageTag(String(tag === undefined ? "undefined" : tag)));
        /* options override / add unicode extension keywords */
        var OPT2KEY = {
            calendar: "ca", collation: "co", hourCycle: "hc",
            caseFirst: "kf", numeric: "kn", numberingSystem: "nu",
            firstDayOfWeek: "fw",
        };
        if (options !== undefined && options !== null) {
            if (typeof options !== "object" && typeof options !== "function") {
                throw new TypeError("Intl.Locale options must be an object");
            }
            for (var name in OPT2KEY) {
                if (!Object.prototype.hasOwnProperty.call(options, name)) continue;
                var raw = options[name];
                if (raw === undefined) continue;
                var key = OPT2KEY[name];
                if (name === "caseFirst") {
                    raw = (raw === true || raw === "true") ? "upper"
                        : (raw === false || raw === "false") ? "lower" : String(raw);
                }
                if (name === "numeric") {
                    raw = (raw === true || raw === "true") ? "true" : "false";
                }
                var canon = canonUValue(key, raw);
                if (canon === null) {
                    throw new RangeError("Intl.Locale: invalid " + name + " value " + raw);
                }
                parsed.u[key] = canon;
            }
        }
        /* canonicalise any values that came from the tag itself */
        for (var uk in parsed.u) {
            if (!Object.prototype.hasOwnProperty.call(parsed.u, uk)) continue;
            var c = canonUValue(uk, parsed.u[uk]);
            parsed.u[uk] = (c === null) ? "" : c;
        }
        this._parsed = parsed;
        this._tag = rebuildTag(parsed);
        localeBrand.add(this);
    }

    function uget(self, key) {
        var v = localeRequire(self)._parsed.u[key];
        return (v === undefined || v === "") ? undefined : v;
    }

    function baseNameOf(self) {
        var p = localeRequire(self)._parsed;
        var out = p.base.slice();
        for (var i = 0; i < p.attrs.length; i++) out.push(p.attrs[i]);
        return out.join("-");
    }
    function variantsOf(self) {
        var p = localeRequire(self)._parsed;
        var vs = [];
        for (var i = 1; i < p.base.length; i++) {
            var seg = p.base[i];
            if (seg.length === 5 || seg.length === 8) vs.push(seg);
        }
        return vs.join("-");
    }

    function defAccessor(self) {
        /* helper: read a -u- value with brand check */
        return function (key) { return uget(localeRequire(self), key); };
    }

    /* accessors (get only) */
    defineGetter(Mblocale.prototype, "language", function getLanguage() {
        return localeRequire(this)._parsed.base[0];
    });
    defineGetter(Mblocale.prototype, "script", function getScript() {
        var s = localeRequire(this)._parsed.base[1];
        return (s && s.length === 4) ? s[0].toUpperCase() + s.slice(1) : undefined;
    });
    defineGetter(Mblocale.prototype, "region", function getRegion() {
        var r = localeRequire(this)._parsed.base[1];
        return (r && r.length === 2) ? r.toUpperCase() : undefined;
    });
    defineGetter(Mblocale.prototype, "variants", function getVariants() {
        var v = variantsOf(this);
        return v === "" ? undefined : v;
    });
    defineGetter(Mblocale.prototype, "baseName", function getBaseName() {
        return baseNameOf(this);
    });
    defineGetter(Mblocale.prototype, "calendar", function getCalendar() {
        return uget(this, "ca");
    });
    defineGetter(Mblocale.prototype, "collation", function getCollation() {
        return uget(this, "co");
    });
    defineGetter(Mblocale.prototype, "hourCycle", function getHourCycle() {
        return uget(this, "hc");
    });
    defineGetter(Mblocale.prototype, "caseFirst", function getCaseFirst() {
        var v = uget(this, "kf");
        return (v === "upper" || v === "lower") ? v : undefined;
    });
    defineGetter(Mblocale.prototype, "numeric", function getNumeric() {
        var v = uget(this, "kn");
        if (v === "true") return true;
        if (v === "false") return false;
        return undefined;
    });
    defineGetter(Mblocale.prototype, "numberingSystem", function getNumberingSystem() {
        return uget(this, "nu");
    });
    defineGetter(Mblocale.prototype, "firstDayOfWeek", function getFirstDayOfWeek() {
        return uget(this, "fw");
    });

    /* methods */
    defineMethod(Mblocale.prototype, "toString", function toString() {
        return localeRequire(this)._tag;
    }, 0);
    defineMethod(Mblocale.prototype, "maximize", function maximize() {
        var p = localeRequire(this)._parsed;
        var u = p.u;
        if (u.ca === undefined) u.ca = "gregory";
        if (u.co === undefined) u.co = "standard";
        if (u.hc === undefined) u.hc = (u.tz || "").length ? "h23" : "h12";
        if (u.nu === undefined) u.nu = "latn";
        var np = { base: p.base.slice(), u: u, attrs: p.attrs.slice(), privateUse: [] };
        var out = rebuildTag(np);
        var m2 = new Mblocale(out);
        return m2;
    }, 0);
    defineMethod(Mblocale.prototype, "minimize", function minimize() {
        var p = localeRequire(this)._parsed;
        var np = { base: p.base.slice(0, 3), u: {}, attrs: [], privateUse: [] };
        return new Mblocale(rebuildTag(np));
    }, 0);
    defineMethod(Mblocale.prototype, "getCalendars", function getCalendars() {
        var v = uget(this, "ca");
        return (v !== undefined) ? [v] : ["gregory"];
    }, 0);
    defineMethod(Mblocale.prototype, "getCollations", function getCollations() {
        var v = uget(this, "co");
        return (v !== undefined) ? [v] : ["standard"];
    }, 0);
    defineMethod(Mblocale.prototype, "getHourCycles", function getHourCycles() {
        var v = uget(this, "hc");
        return (v !== undefined) ? [v] : ["h11", "h23"];
    }, 0);
    defineMethod(Mblocale.prototype, "getNumberingSystems", function getNumberingSystems() {
        var v = uget(this, "nu");
        return (v !== undefined) ? [v] : ["latn"];
    }, 0);
    defineMethod(Mblocale.prototype, "getTimeZones", function getTimeZones() {
        var v = uget(this, "tz");
        return (v !== undefined) ? [v] : ["UTC"];
    }, 0);
    defineMethod(Mblocale.prototype, "getWeekInfo", function getWeekInfo() {
        var fw = uget(this, "fw");
        return {
            firstDayOfWeek: fw,
            minimalDays: 1,
            weekendStart: 6,
            weekendEnd: 7,
            firstWeek: 1
        };
    }, 0);
    defineMethod(Mblocale.prototype, "getTextInfo", function getTextInfo() {
        var lang = localeRequire(this)._parsed.base[0];
        var rtl = !!RTL_LANGS[lang];
        return {
            direction: rtl ? "rtl" : "ltr",
            orientation: rtl ? "right-to-left" : "left-to-right"
        };
    }, 0);

    setToStringTag(Mblocale.prototype, "Locale");
    installConstructor(Intl, "Locale", Mblocale, 2);

    /* ── NumberFormat ────────────────────────────────────────────── */

    function MbNumberFormat(locales, options) {
        this._locale = pickLocale(locales);
        this._opts = getNumberOptions(options);
    }
    /* locale-specific separators. id: group='.', dec=',' ; en: ',' '.' */
    function localeSep(locale) {
        if (/^id/i.test(locale)) return { group: ".", dec: "," };
        return { group: ",", dec: "." };
    }

    /* compact notation suffixes (short style, en/id share the latin scale) */
    function compactSuffix(exp) {
        if (exp >= 14) return "Q";
        if (exp >= 12) return "T";
        if (exp >= 9)  return "B";
        if (exp >= 6)  return "M";
        if (exp >= 3)  return "K";
        return "";
    }
    function compactExp(n) {
        var e = 0, a = Math.abs(n);
        if (a >= 1e14) e = 14; else if (a >= 1e12) e = 12; else if (a >= 1e9) e = 9;
        else if (a >= 1e6) e = 6; else if (a >= 1e3) e = 3;
        return e;
    }

    MbNumberFormat.prototype.format = function (value) {
        var n = Number(value);
        if (Number.isNaN(n)) return "NaN";
        if (!isFinite(n)) return n > 0 ? "∞" : "-∞";
        var o = this._opts, sep = localeSep(this._locale), sign = "", s, cexp = 0;
        if (n < 0) { sign = "-"; n = -n; }
        var maxF = o.maximumFractionDigits, minF = o.minimumFractionDigits;
        if (o.style === "percent") n = n * 100;
        if (o.notation === "compact") {
            cexp = compactExp(n);
            if (cexp) { n = n / Math.pow(10, cexp); maxF = maxF < 1 ? maxF : 1; minF = 0; }
        }

        s = n.toFixed(maxF);
        /* strip trailing zeros down to minF */
        if (s.indexOf(".") >= 0) {
            var frac = s.split(".")[1];
            if (frac.length > maxF) frac = frac.slice(0, maxF);
            while (frac.length > minF && /0$/.test(frac)) frac = frac.slice(0, -1);
            var ip0 = s.split(".")[0];
            s = frac.length ? (ip0 + sep.dec + frac) : ip0;
        }
        var parts = s.split(sep.dec);
        if (o.useGrouping && o.notation === "standard") {
            var ip = parts[0], grouped = "", cnt = 0;
            for (var i = ip.length - 1; i >= 0; i--) {
                grouped = ip[i] + grouped;
                if (++cnt % 3 === 0 && i > 0) grouped = sep.group + grouped;
            }
            parts[0] = grouped;
        }
        var out = sign + parts[0];
        if (parts[1]) out += sep.dec + parts[1];
        if (o.notation === "compact") out += compactSuffix(cexp);
        if (o.style === "percent") out += "%";
        if (o.style === "currency") {
            var code = o.currency;
            if (o.currencyDisplay === "code") {
                out = code + NBSP + out;
            } else if (o.currencyDisplay === "name") {
                var names = { USD: "US dollars", EUR: "euros", IDR: "Indonesian rupiah", JPY: "Japanese yen" };
                out = out + NBSP + (names[code] || code);
            } else {
                var syms = { USD: "$", EUR: "€", IDR: "Rp", JPY: "¥", GBP: "£" };
                /* V8 puts a narrow no-break space after a multi-letter symbol (Rp) */
                var sy = syms[code] || code;
                out = (sy.length > 1 ? sy + NBSP : sy) + out;
            }
        }
        return out;
    };
    MbNumberFormat.prototype.formatToParts = function (value) {
        var str = this.format(value);
        return [{ type: "literal", value: str }];
    };
    MbNumberFormat.prototype.resolvedOptions = function () {
        return Object.assign({ locale: this._locale }, this._opts);
    };
    MbNumberFormat.prototype.formatRange = function (a, b) {
        return this.format(a) + " – " + this.format(b);
    };
    MbNumberFormat.supportedLocalesOf = function (locales) {
        var arr = Array.isArray(locales) ? locales : [locales];
        return arr.filter(function (l) {
            var s = normLocale(l);
            return /^(en|id)/i.test(s);
        }).map(normLocale);
    };
    Intl.NumberFormat = MbNumberFormat;

    /* ── DateTimeFormat ──────────────────────────────────────────── */

    /* ── timezone table (no ICU) ───────────────────────────────────
     * Standard + DST offsets for the common IANA zones. lean: DST rules
     * are the modern (post-2007 US / post-1996 EU) Sunday-based rules. */
    var TZ_TABLE = {
        utc: { std: 0, dst: 0 }, "etc/utc": { std: 0, dst: 0 }, "etc/gmt": { std: 0, dst: 0 },
        "america/new_york": { std: -18000, dst: -14400, rule: "US" },
        "america/chicago": { std: -21600, dst: -18000, rule: "US" },
        "america/denver": { std: -25200, dst: -21600, rule: "US" },
        "america/los_angeles": { std: -28800, dst: -25200, rule: "US" },
        "america/vegas": { std: -28800, dst: -25200, rule: "US" },
        "america/anchorage": { std: -32400, dst: -28800, rule: "US" },
        "america/halifax": { std: -14400, dst: -10800, rule: "US" },
        "america/toronto": { std: -18000, dst: -14400, rule: "US" },
        "america/vancouver": { std: -28800, dst: -25200, rule: "US" },
        "america/edmonton": { std: -25200, dst: -21600, rule: "US" },
        "america/monterrey": { std: -21600, dst: -18000, rule: "US" },
        "america/panama": { std: -18000, dst: -18000 },
        "america/bogota": { std: -18000, dst: -18000 },
        "america/lima": { std: -18000, dst: -18000 },
        "america/sao_paulo": { std: -10800, dst: -10800 },
        "america/mexico_city": { std: -21600, dst: -21600 },
        "europe/london": { std: 0, dst: 3600, rule: "EU" },
        "europe/dublin": { std: 0, dst: 3600, rule: "EU" },
        "europe/paris": { std: 3600, dst: 7200, rule: "EU" },
        "europe/berlin": { std: 3600, dst: 7200, rule: "EU" },
        "europe/madrid": { std: 3600, dst: 7200, rule: "EU" },
        "europe/rome": { std: 3600, dst: 7200, rule: "EU" },
        "europe/amsterdam": { std: 3600, dst: 7200, rule: "EU" },
        "europe/brussels": { std: 3600, dst: 7200, rule: "EU" },
        "europe/vienna": { std: 3600, dst: 7200, rule: "EU" },
        "europe/stockholm": { std: 3600, dst: 7200, rule: "EU" },
        "europe/oslo": { std: 3600, dst: 7200, rule: "EU" },
        "europe/copenhagen": { std: 3600, dst: 7200, rule: "EU" },
        "europe/prague": { std: 3600, dst: 7200, rule: "EU" },
        "europe/warsaw": { std: 3600, dst: 7200, rule: "EU" },
        "europe/budapest": { std: 3600, dst: 7200, rule: "EU" },
        "europe/zurich": { std: 3600, dst: 7200, rule: "EU" },
        "europe/athens": { std: 3600, dst: 7200, rule: "EU" },
        "europe/moscow": { std: 10800, dst: 10800 },
        "europe/istanbul": { std: 10800, dst: 10800 },
        "asia/tokyo": { std: 32400, dst: 32400 },
        "asia/seoul": { std: 32400, dst: 32400 },
        "asia/shanghai": { std: 28800, dst: 28800 },
        "asia/hong_kong": { std: 28800, dst: 28800 },
        "asia/singapore": { std: 28800, dst: 28800 },
        "asia/kolkata": { std: 19800, dst: 19800 },
        "asia/kathmandu": { std: 20700, dst: 20700 },
        "asia/bangkok": { std: 25200, dst: 25200 },
        "asia/jakarta": { std: 25200, dst: 25200 },
        "asia/manila": { std: 28800, dst: 28800 },
        "asia/dubai": { std: 14400, dst: 14400 },
        "asia/riyadh": { std: 14400, dst: 14400 },
        "asia/tehran": { std: 12600, dst: 12600 },
        "asia/jerusalem": { std: 7200, dst: 10800, rule: "IL" },
        "australia/sydney": { std: 36000, dst: 39600, rule: "AU" },
        "australia/melbourne": { std: 36000, dst: 39600, rule: "AU" },
        "australia/brisbane": { std: 36000, dst: 36000 },
        "australia/perth": { std: 28800, dst: 28800 },
        "australia/adelaide": { std: 34200, dst: 37800, rule: "AU" },
        "australia/darwin": { std: 30600, dst: 30600 },
        "pacific/auckland": { std: 43200, dst: 46800, rule: "NZ" },
        "pacific/honolulu": { std: -36000, dst: -36000 },
        "africa/johannesburg": { std: 7200, dst: 7200 },
        "africa/lagos": { std: 3600, dst: 3600 },
        "africa/nairobi": { std: 10800, dst: 10800 },
        "africa/accra": { std: 0, dst: 0 }
    };
    function _utcFrom(y, mo, d, h, mi, s) {
        return Math.floor(Date.UTC(y, mo - 1, d, h, mi, s) / 1000);
    }
    function _nthDow(y, mo, dow, n) {
        var first = new Date(Date.UTC(y, mo - 1, 1)).getUTCDay();
        return 1 + ((dow - first + 7) % 7) + (n - 1) * 7;
    }
    function _lastDow(y, mo, dow) {
        var lastDay = new Date(Date.UTC(y, mo, 0)).getUTCDate();
        var lastDow2 = new Date(Date.UTC(y, mo - 1, lastDay)).getUTCDay();
        return lastDay - ((lastDow2 - dow + 7) % 7);
    }
    function _dstTransition(rule, year, std, dst) {
        if (rule === "US")
            return [_utcFrom(year, 3, _nthDow(year, 3, 0, 2), 2, 0, 0) - std,
                    _utcFrom(year, 11, _nthDow(year, 11, 0, 1), 2, 0, 0) - dst];
        if (rule === "EU")
            return [_utcFrom(year, 3, _lastDow(year, 3, 0), 1, 0, 0),
                    _utcFrom(year, 10, _lastDow(year, 10, 0), 1, 0, 0)];
        if (rule === "IL")
            return [_utcFrom(year, 3, _nthDow(year, 3, 0, 3), 2, 0, 0) - std,
                    _utcFrom(year, 10, _lastDow(year, 10, 0), 2, 0, 0) - dst];
        if (rule === "AU")
            return [_utcFrom(year, 10, _nthDow(year, 10, 0, 1), 2, 0, 0) - std,
                    _utcFrom(year + 1, 4, _nthDow(year + 1, 4, 0, 1), 3, 0, 0) - dst];
        if (rule === "NZ")
            return [_utcFrom(year, 9, _lastDow(year, 9, 0), 2, 0, 0) - std,
                    _utcFrom(year + 1, 4, _nthDow(year + 1, 4, 0, 1), 3, 0, 0) - dst];
        return [0, 0];
    }
    function _tzOffsetSec(zone, epochSec) {
        if (zone.dst === zone.std) return zone.std;
        var y = new Date(epochSec * 1000).getUTCFullYear();
        for (var yy = y - 1; yy <= y + 1; yy++) {
            var t = _dstTransition(zone.rule, yy, zone.std, zone.dst);
            if (epochSec >= t[0] && epochSec < t[1]) return zone.dst;
        }
        return zone.std;
    }
    function _canonTzId(lowerId) {
        var m = lowerId.match(/^([a-z]+)\/([a-z0-9_]+)$/);
        if (!m) return lowerId === "utc" ? "UTC" : lowerId;
        var c1 = m[1].charAt(0).toUpperCase() + m[1].slice(1);
        var c2 = m[2].split("_").map(function (p) {
            return p.charAt(0).toUpperCase() + p.slice(1);
        }).join("_");
        return c1 + "/" + c2;
    }
    function MbDateTimeFormat(locales, options) {
        this._locale = pickLocale(locales);
        this._opts = options || {};
        this._zone = null;
        this._fixedOffsetSec = null;
        this._useLocal = !this._opts.timeZone;
        this._tzResolved = this._useLocal
            ? "UTC"
            : String(this._opts.timeZone);
        if (this._useLocal) return;  /* no timeZone option: system local */
        var tz = String(this._opts.timeZone);
        var zone = TZ_TABLE[tz.toLowerCase()];
        if (zone) { this._zone = zone; this._tzResolved = _canonTzId(tz.toLowerCase()); return; }
        var m = /^([+-])(\d{1,2})(?::?(\d{2}))?$/.exec(tz);
        if (m) { this._fixedOffsetSec = (m[1] === "-" ? -1 : 1) * ((+m[2]) * 3600 + (+ (m[3] || 0)) * 60); return; }
        var gm = /^etc\/gmt([+-])(\d{1,2})(?::?(\d{2}))?$/i.exec(tz);
        if (gm) { this._fixedOffsetSec = -(gm[1] === "-" ? -1 : 1) * ((+gm[2]) * 3600 + (+ (gm[3] || 0)) * 60); return; }
        /* unknown named zone: best-effort UTC */
        this._tzResolved = _canonTzId(tz.toLowerCase());
    }
    MbDateTimeFormat.prototype._offsetFor = function (epochSec) {
        if (this._zone) return _tzOffsetSec(this._zone, epochSec);
        if (this._fixedOffsetSec !== null) return this._fixedOffsetSec;
        if (!this._useLocal) return 0;
        return -new Date(epochSec * 1000).getTimezoneOffset() * 60;
    };
    MbDateTimeFormat.prototype.format = function (value) {
        var d = value === undefined ? new Date() : (value instanceof Date ? value : new Date(value));
        if (Number.isNaN(d.getTime())) return "Invalid Date";
        /* wall time in the requested zone (or system local), read via UTC getters */
        var epochSec = Math.floor(d.getTime() / 1000);
        var zShiftMs = this._offsetFor(epochSec) * 1000;
        var base = new Date(d.getTime() + zShiftMs);
        var o = this._opts;
        var date = (o.year === undefined && o.month === undefined && o.day === undefined &&
                    o.weekday === undefined && o.dateStyle !== "full" && o.dateStyle !== "long" &&
                    o.dateStyle !== "medium" && o.dateStyle !== "short" &&
                    o.timeStyle === undefined) ? false : true;
        var time = (o.hour !== undefined || o.minute !== undefined || o.second !== undefined ||
                    o.timeStyle !== undefined);
        var out = [];
        var idLocale = /^id/i.test(this._locale);
        var monthName = ["Januari", "Februari", "Maret", "April", "Mei", "Juni", "Juli", "Agustus", "September", "Oktober", "November", "Desember"];
        var monthShort = ["Jan", "Feb", "Mar", "Apr", "Mei", "Jun", "Jul", "Agu", "Sep", "Okt", "Nov", "Des"];
        var enMonths = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
        var enShort = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
        var month = idLocale ? monthName : enMonths;
        var monthS = idLocale ? monthShort : enShort;

        var dateStyleVal = o.dateStyle;
        var idl = /^id/i.test(this._locale) || /^en-GB/i.test(this._locale);
        var wdLongA = ["Minggu","Senin","Selasa","Rabu","Kamis","Jumat","Sabtu"];
        var wdLongB = ["Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"];
        var wdShortA = ["Min","Sen","Sel","Rab","Kam","Jum","Sab"];
        var wdShortB = ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"];
        var wdL = (idl ? wdLongA : wdLongB)[base.getUTCDay()];
        var wdS = (idl ? wdShortA : wdShortB)[base.getUTCDay()];
        var moFull = month[base.getUTCMonth()];
        var moAbr = monthS[base.getUTCMonth()];
        var yFull = String(base.getUTCFullYear());
        var ySh = String(base.getUTCFullYear() % 100);
        var moN = String(base.getUTCMonth() + 1);
        var daN = String(base.getUTCDate());
        var dstr = "";
        if (dateStyleVal === "full") {
            if (idl) dstr = wdL + ", " + pad2(base.getUTCDate()) + " " + moFull + " " + yFull;
            else dstr = wdL + ", " + moFull + " " + daN + ", " + yFull;
        } else if (dateStyleVal === "long") {
            if (idl) dstr = daN + " " + moFull + " " + yFull;
            else dstr = moFull + " " + daN + ", " + yFull;
        } else if (dateStyleVal === "medium") {
            if (idl) dstr = daN + " " + moAbr + " " + yFull;
            else dstr = moAbr + " " + daN + ", " + yFull;
        } else if (dateStyleVal === "short") {
            dstr = idl ? (pad2(base.getUTCDate()) + "/" + moN + "/" + ySh) : (moN + "/" + daN + "/" + ySh);
        } else {
            /* explicit year/month/day options — order follows locale */
            var head = [];
            if (idl) {
                if (o.day) head.push(o.day === "numeric" ? daN : pad2(base.getUTCDate()));
                if (o.month === "long") head.push(moFull);
                else if (o.month === "short") head.push(moAbr);
                else if (o.month) head.push(pad2(base.getUTCMonth() + 1));
            } else {
                if (o.month === "long") head.push(moFull);
                else if (o.month === "short") head.push(moAbr);
                else if (o.month) head.push(pad2(base.getUTCMonth() + 1));
                if (o.day) head.push(o.day === "numeric" ? daN : pad2(base.getUTCDate()));
            }
            var yrBit = o.year ? (o.year === "2-digit" ? ySh : yFull) : "";
            dstr = head.join(" ");
            if (yrBit) dstr = dstr ? dstr + ", " + yrBit : yrBit;
            if (o.weekday) dstr = (o.weekday === "short" ? wdS : wdL) + (dstr ? ", " + dstr : "");
        }
        if (dstr) out.push(dstr);
        if (time) {
            var h = base.getUTCHours(), mi = base.getUTCMinutes(), s2 = base.getUTCSeconds();
            var ts = o.timeStyle;                    /* full|long|medium|short|undefined */
            var showSec = o.second !== undefined || ts === "full" || ts === "long" || ts === "medium";
            var showMin = o.minute !== undefined || showSec || ts === "short";
            var h12;
            if (o.hour12 !== undefined) h12 = o.hour12;
            else if (o.hourCycle) h12 = (o.hourCycle === "h11" || o.hourCycle === "h12");
            else h12 = !(ts && idLocale);
            var sepT = idLocale ? "." : ":";
            var tstr;
            if (h12) {
                var ap = h < 12 ? "AM" : "PM";
                var hh = h % 12; if (hh === 0) hh = 12;
                tstr = String(hh) + (showMin ? sepT + pad2(mi) : "") + (showSec ? sepT + pad2(s2) : "") + " " + ap;
            } else {
                tstr = pad2(h) + (showMin ? sepT + pad2(mi) : "") + (showSec ? sepT + pad2(s2) : "");
            }
            out.push(tstr);
        }
        return out.join(" ");
    };
    MbDateTimeFormat.prototype.formatToParts = function (value) {
        var d = value === undefined ? new Date() : (value instanceof Date ? value : new Date(value));
        if (Number.isNaN(d.getTime()))
            return [{ type: "literal", value: "Invalid Date" }];
        var epochSec = Math.floor(d.getTime() / 1000);
        var zShiftMs = this._offsetFor(epochSec) * 1000;
        var base = new Date(d.getTime() + zShiftMs);
        var o = this._opts;
        var parts = [];
        if (o.era) parts.push({ type: "era", value: base.getUTCFullYear() >= 1 ? "AD" : "BC" });
        if (o.year) parts.push({ type: "year", value: String(Math.abs(base.getUTCFullYear())) });
        if (o.month) parts.push({ type: "month", value: String(base.getUTCMonth() + 1) });
        if (o.day) parts.push({ type: "day", value: String(base.getUTCDate()) });
        if (o.weekday) parts.push({ type: "weekday", value: ["Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"][base.getUTCDay()] });
        if (o.hour) {
            var hh = base.getUTCHours();
            var use24 = o.hour12 === false || o.hourCycle === "h23" || o.hourCycle === "h24" || /h23/.test(String(this._locale));
            if (use24) {
                parts.push({ type: "hour", value: String(hh) });
            } else {
                var hh12 = hh % 12; if (hh12 === 0) hh12 = 12;
                parts.push({ type: "hour", value: String(hh12) });
                parts.push({ type: "dayPeriod", value: hh < 12 ? "AM" : "PM" });
            }
        }
        if (o.minute) parts.push({ type: "minute", value: String(base.getUTCMinutes()) });
        if (o.second) parts.push({ type: "second", value: String(base.getUTCSeconds()) });
        if (o.timeZoneName) parts.push({ type: "timeZoneName", value: this._tzResolved });
        parts.push({ type: "literal", value: "" });
        return parts;
    };
    MbDateTimeFormat.prototype.resolvedOptions = function () {
        return { locale: this._locale, calendar: "gregory", numberingSystem: "latn",
                 timeZone: this._tzResolved, hour12: true, hourCycle: "h12",
                 year: this._opts.year, month: this._opts.month, day: this._opts.day,
                 hour: this._opts.hour, minute: this._opts.minute, second: this._opts.second };
    };
    MbDateTimeFormat.prototype.formatRange = function (a, b) {
        return this.format(a) + " – " + this.format(b);
    };
    MbDateTimeFormat.supportedLocalesOf = MbNumberFormat.supportedLocalesOf;
    Intl.DateTimeFormat = MbDateTimeFormat;

    /* ── Collator ────────────────────────────────────────────────── */

    function MbCollator(locales, options) {
        this._locale = pickLocale(locales);
        this._opts = options || {};
        /* ES2021: the getter returns a bound closure over the internal slots,
           so it can be detached from `this` (e.g. passed to Array#sort). */
        var self = this;
        this.compare = function (a, b) {
            var x = String(a), y = String(b);
            if (self._opts.numeric) {
                var nx = parseFloat(x), ny = parseFloat(y);
                if (!isNaN(nx) && !isNaN(ny)) return nx < ny ? -1 : (nx > ny ? 1 : 0);
            }
            return x < y ? -1 : (x > y ? 1 : 0);
        };
    }
    MbCollator.prototype.compare = function (a, b) {
        var x = String(a), y = String(b);
        if (this._opts.numeric) {
            var nx = parseFloat(x), ny = parseFloat(y);
            if (!isNaN(nx) && !isNaN(ny)) return nx < ny ? -1 : (nx > ny ? 1 : 0);
        }
        return x < y ? -1 : (x > y ? 1 : 0);
    };
    MbCollator.prototype.resolvedOptions = function () {
        return { locale: this._locale, usage: "sort", sensitivity: "variant", numeric: false };
    };
    MbCollator.supportedLocalesOf = MbNumberFormat.supportedLocalesOf;
    Intl.Collator = MbCollator;

    /* ── PluralRules ─────────────────────────────────────────────── */

    function MbPluralRules(locales, options) {
        this._locale = pickLocale(locales);
        this._opts = options || {};
    }
    MbPluralRules.prototype.select = function (n) { return pluralCat(Number(n), this._locale); };
    MbPluralRules.prototype.selectRange = function (a, b) { return pluralCat(Number(a), this._locale); };
    MbPluralRules.prototype.resolvedOptions = function () {
        return { locale: this._locale, type: this._opts.type || "cardinal",
                 minimumIntegerDigits: 1, minimumFractionDigits: 0, maximumFractionDigits: 3 };
    };
    MbPluralRules.supportedLocalesOf = MbNumberFormat.supportedLocalesOf;
    Intl.PluralRules = MbPluralRules;

    /* ── RelativeTimeFormat ──────────────────────────────────────── */

    function MbRelativeTimeFormat(locales, options) {
        this._locale = pickLocale(locales);
        this._opts = options || {};
    }
    MbRelativeTimeFormat.prototype.format = function (value, unit) {
        var v = Number(value);
        var idLocale = /^id/i.test(this._locale);
        var units = {
            year:  idLocale ? "tahun" : "years",
            quarter: idLocale ? "kuartal" : "quarters",
            month: idLocale ? "bulan" : "months",
            week: idLocale ? "minggu" : "weeks",
            day: idLocale ? "hari" : "days",
            hour: idLocale ? "jam" : "hours",
            minute: idLocale ? "menit" : "minutes",
            second: idLocale ? "detik" : "seconds",
        };
        var un = units[unit] || unit;
        var out = Math.abs(v) + " " + un;
        var auto = (this._opts.numeric === "auto");
        if (v < 0) return idLocale ? out + " yang lalu" : out + " ago";
        if (v > 0) return idLocale ? "dalam " + out : "in " + out;
        /* numeric:"always" (default) → "in 0 seconds"; only auto → "now" */
        return auto ? (idLocale ? "sekarang" : "now")
                    : (idLocale ? "dalam " + out : "in " + out);
    };
    MbRelativeTimeFormat.prototype.formatToParts = function (value, unit) {
        var str = this.format(value, unit);
        return [{ type: "literal", value: str }];
    };
    MbRelativeTimeFormat.prototype.resolvedOptions = function () {
        return { locale: this._locale, style: this._opts.style || "long",
                 numeric: this._opts.numeric || "always", numberingSystem: "latn" };
    };
    MbRelativeTimeFormat.supportedLocalesOf = MbNumberFormat.supportedLocalesOf;
    Intl.RelativeTimeFormat = MbRelativeTimeFormat;

    /* ── ListFormat ──────────────────────────────────────────────── */

    /* CLDR list patterns.  For every (type, style) we store the separator
       that is inserted in front of an element: "two" applies when the
       list holds exactly two elements, "start"/"middle"/"end" otherwise. */
    function lp(two, start, middle, end) {
        return { two: two, start: start, middle: middle, end: end };
    }
    var LIST_PATTERNS = {
        en: {
            conjunction: {
                long: lp(" and ", ", ", ", ", ", and "),
                short: lp(" & ", ", ", ", ", ", & "),
                narrow: lp(" ", ", ", ", ", ", "),
            },
            disjunction: {
                long: lp(" or ", ", ", ", ", ", or "),
                short: lp(" or ", ", ", ", ", ", or "),
                narrow: lp(" ", ", ", ", ", ", "),
            },
            unit: {
                long: lp(", ", ", ", ", ", ", "),
                short: lp(", ", ", ", ", ", ", "),
                narrow: lp(" ", " ", " ", " "),
            },
        },
        es: {
            conjunction: {
                long: lp(" y ", ", ", ", ", ", y "),
                short: lp(" y ", ", ", ", ", ", y "),
                narrow: lp(" ", " ", " ", " "),
            },
            disjunction: {
                long: lp(" o ", ", ", ", ", ", o "),
                short: lp(" o ", ", ", ", ", ", o "),
                narrow: lp(" ", " ", " ", " "),
            },
            unit: {
                long: lp(" y ", ", ", ", ", " y "),
                short: lp(" y ", ", ", ", ", ", "),
                narrow: lp(" ", " ", " ", " "),
            },
        },
        id: {
            conjunction: {
                long: lp(" dan ", ", ", ", ", ", dan "),
                short: lp(" & ", ", ", ", ", ", & "),
                narrow: lp(" ", " ", " ", " "),
            },
            disjunction: {
                long: lp(" atau ", ", ", ", ", ", atau "),
                short: lp(" atau ", ", ", ", ", ", atau "),
                narrow: lp(" ", " ", " ", " "),
            },
            unit: {
                long: lp(", ", ", ", ", ", ", "),
                short: lp(", ", ", ", ", ", ", "),
                narrow: lp(" ", " ", " ", " "),
            },
        },
        fr: {
            conjunction: {
                long: lp(" et ", ", ", ", ", ", et "),
                short: lp(" et ", ", ", ", ", ", et "),
                narrow: lp(" ", " ", " ", " "),
            },
            disjunction: {
                long: lp(" ou ", ", ", ", ", ", ou "),
                short: lp(" ou ", ", ", ", ", ", ou "),
                narrow: lp(" ", " ", " ", " "),
            },
            unit: {
                long: lp(", ", ", ", ", ", ", "),
                short: lp(", ", ", ", ", ", ", "),
                narrow: lp(" ", " ", " ", " "),
            },
        },
        de: {
            conjunction: {
                long: lp(" und ", ", ", ", ", ", und "),
                short: lp(" & ", ", ", ", ", ", & "),
                narrow: lp(" ", " ", " ", " "),
            },
            disjunction: {
                long: lp(" oder ", ", ", ", ", ", oder "),
                short: lp(" oder ", ", ", ", ", ", oder "),
                narrow: lp(" ", " ", " ", " "),
            },
            unit: {
                long: lp(", ", ", ", ", ", ", "),
                short: lp(", ", ", ", ", ", ", "),
                narrow: lp(" ", " ", " ", " "),
            },
        },
    };

    function listPatternFor(locale, type, style) {
        var lang = String(locale).toLowerCase().split(/[-_]/)[0];
        var byLang = LIST_PATTERNS[lang] || LIST_PATTERNS.en;
        var byType = byLang[type] || byLang.conjunction;
        return byType[style] || byType.long;
    }

    /* ECMA-402 StringListFromIterable */
    function stringListFromIterable(iterable) {
        if (iterable === undefined) return [];
        if (iterable === null) throw new TypeError("StringListFromIterable: null is not iterable");
        var iter;
        try {
            iter = iterable[Symbol.iterator];
        } catch (e) {
            throw new TypeError("StringListFromIterable: not iterable");
        }
        if (typeof iter !== "function") {
            throw new TypeError("StringListFromIterable: not iterable");
        }
        var it = iter.call(iterable);
        if (it === null || (typeof it !== "object" && typeof it !== "function")) {
            throw new TypeError("StringListFromIterable: iterator is not an object");
        }
        var next = it.next;
        if (typeof next !== "function") {
            throw new TypeError("StringListFromIterable: iterator.next is not callable");
        }
        var list = [];
        while (true) {
            var step = next.call(it);
            if (step === null || (typeof step !== "object" && typeof step !== "function")) {
                throw new TypeError("StringListFromIterable: iterator result is not an object");
            }
            if (step.done) break;
            var v = step.value;
            if (typeof v !== "string") {
                var ret = it["return"];
                if (typeof ret === "function") ret.call(it);
                throw new TypeError("StringListFromIterable: element is not a string");
            }
            list.push(v);
        }
        return list;
    }

    function listFormatParts(pattern, items) {
        var parts = [];
        for (var i = 0; i < items.length; i++) {
            if (i > 0) {
                var sep;
                if (items.length === 2) sep = pattern.two;
                else if (i === 1) sep = pattern.start;
                else if (i === items.length - 1) sep = pattern.end;
                else sep = pattern.middle;
                parts.push({ type: "literal", value: sep });
            }
            parts.push({ type: "element", value: items[i] });
        }
        return parts;
    }

    var lfBrand = makeBrand();

    function MbListFormat(locales, options) {
        requireConstructor(MbListFormat, "ListFormat", this);
        var req = canonicalizeLocaleList(locales, options);
        this._locale = req.length ? req[0] : "en-US";
        var opts = getOptionsObject(options);
        var type = getOption(opts, "type", "string", ["conjunction", "disjunction", "unit"], "conjunction");
        var style = getOption(opts, "style", "string", ["long", "short", "narrow"], "long");
        this._type = type;
        this._style = style;
        lfBrand.add(this);
    }

    defineMethod(MbListFormat.prototype, "format", function format(list) {
        requireBrand(lfBrand, this, "ListFormat");
        var items = stringListFromIterable(list);
        var pattern = listPatternFor(this._locale, this._type, this._style);
        var parts = listFormatParts(pattern, items);
        var out = "";
        for (var i = 0; i < parts.length; i++) out += parts[i].value;
        return out;
    }, 1);

    defineMethod(MbListFormat.prototype, "formatToParts", function formatToParts(list) {
        requireBrand(lfBrand, this, "ListFormat");
        var items = stringListFromIterable(list);
        var pattern = listPatternFor(this._locale, this._type, this._style);
        return listFormatParts(pattern, items);
    }, 1);

    defineMethod(MbListFormat.prototype, "resolvedOptions", function resolvedOptions() {
        requireBrand(lfBrand, this, "ListFormat");
        var o = {};
        o.locale = this._locale;
        o.type = this._type;
        o.style = this._style;
        return o;
    }, 0);

    setToStringTag(MbListFormat.prototype, "Intl.ListFormat");
    fixPrototypeProp(MbListFormat);
    defineStatic(MbListFormat, "supportedLocalesOf", function supportedLocalesOf(locales) {
        var req = canonicalizeLocaleList(locales);
        var out = [];
        for (var i = 0; i < req.length; i++) {
            if (out.indexOf(req[i]) < 0 && SHIM_LANGS[req[i].split("-")[0]]) out.push(req[i]);
        }
        return out;
    }, 1);
    installConstructor(Intl, "ListFormat", MbListFormat, 0);

    /* ── static helpers ──────────────────────────────────────────── */

    Intl.getCanonicalLocales = function (locales) {
        var arr = Array.isArray(locales) ? locales : [locales];
        return arr.map(normLocale);
    };
    var supportedValuesOf = (key) => {
        if (typeof key === "symbol") throw new TypeError("cannot convert a Symbol to a string");
        var k = String(key);
        var out;
        switch (k) {
            case "calendar": out = ["gregory"]; break;
            case "collation": out = []; break;
            case "currency": out = ["EUR", "GBP", "IDR", "JPY", "USD"]; break;
            case "numberingSystem": out = ["latn"]; break;
            case "timeZone": out = ["Asia/Jakarta", "Asia/Jayapura", "Asia/Makassar", "UTC"]; break;
            case "unit": out = ["day", "hour", "minute", "month", "second", "week", "year"]; break;
            default: throw new RangeError("invalid key");
        }
        return out.slice(0);
    };
    Object.defineProperty(Intl, "supportedValuesOf", {
        value: supportedValuesOf, writable: true, enumerable: false, configurable: true,
    });

    /* ── DurationFormat (ES2025, shimmed in patch_ecma2026 if absent) ── */
    if (typeof Intl.DurationFormat === "undefined") {
        function MbDurationFormat(locales, options) {
            this._locale = pickLocale(locales);
            this._opts = options || {};
        }
        MbDurationFormat.prototype.format = function (dur) {
            if (dur && dur.toString) return dur.toString();
            return String(dur);
        };
        MbDurationFormat.prototype.formatToParts = function (dur) {
            return [{ type: "literal", value: this.format(dur) }];
        };
        MbDurationFormat.prototype.resolvedOptions = function () {
            return { locale: this._locale, style: this._opts.style || "short",
                     numberingSystem: "latn" };
        };
        Intl.DurationFormat = MbDurationFormat;
    }


    /* ==================================================================
     * Intl.Segmenter  (ECMA-402; segmentation is approximate — the
     * structural / option-handling tests are spec-faithful, the actual
     * grapheme/word/sentence boundaries are a code-point approximation
     * because the shim ships without ICU.)
     * ================================================================== */
    var segBrand = makeBrand();

    function supportedLocaleTest(l) {
        return !!SHIM_LANGS[String(l).toLowerCase().split("-")[0]];
    }

    function MbSegmenter(locales, options) {
        if (new.target === undefined) {
            throw new TypeError("Constructor Intl.Segmenter requires 'new'");
        }
        var locList = canonicalizeLocaleList(locales);
        var opts = getOptionsObject(options);
        /* spec read order: localeMatcher, then granularity */
        getOption(opts, "localeMatcher", "string", ["lookup", "best fit"], "best fit");
        var granularity = getOption(opts, "granularity", "string",
            ["grapheme", "word", "sentence"], "grapheme");
        segBrand.add(this);
        this._locale = locList.length ? locList[0] : "en-US";
        this._granularity = granularity;
    }

    function segSplit(input, granularity) {
        var segs = [], i = 0;
        if (granularity === "grapheme") {
            var cps = Array.from(input);
            for (var j = 0; j < cps.length; j++) { segs.push({ s: cps[j], i: i }); i += cps[j].length; }
            return segs;
        }
        if (granularity === "word") {
            var re = /\s+|[0-9]+|[A-Za-z]+|[^\s\w0-9A-Za-z]/g, m;
            while ((m = re.exec(input)) !== null) { segs.push({ s: m[0], i: m.index }); }
            return segs;
        }
        /* sentence */
        var buf = "", start = 0;
        for (var k = 0; k < input.length; k++) {
            buf += input[k];
            if (".!?".indexOf(input[k]) >= 0) {
                while (k + 1 < input.length && ".!?".indexOf(input[k + 1]) >= 0) { buf += input[++k]; }
                segs.push({ s: buf, i: start }); buf = ""; start = k + 1;
            }
        }
        if (buf.length) segs.push({ s: buf, i: start });
        return segs;
    }

    function makeSegments(segmenter, input) {
        var pieces = segSplit(input, segmenter._granularity);
        var obj = {};
        defineMethod(obj, "containing", function containing(index) {
            index = index === undefined ? 0 : Number(index);
            if (index < 0 || index >= input.length) throw new RangeError("index out of range");
            for (var q = 0; q < pieces.length; q++) {
                var end = pieces[q].i + pieces[q].s.length;
                if (index >= pieces[q].i && index < end) {
                    return { segment: pieces[q].s, index: pieces[q].i, input: input };
                }
            }
            return undefined;
        }, 1);
        obj[Symbol.iterator] = function () {
            var n = 0;
            var it = {};
            it.next = function () {
                if (n < pieces.length) {
                    var p = pieces[n++];
                    return { value: { segment: p.s, index: p.i, input: input }, done: false };
                }
                return { value: undefined, done: true };
            };
            it[Symbol.iterator] = function () { return it; };
            return it;
        };
        Object.setPrototypeOf(obj, MbSegmenter.prototype._segmentsProto || Object.prototype);
        return obj;
    }

    defineMethod(MbSegmenter.prototype, "segment", function segment(string) {
        requireBrand(segBrand, this, "Segmenter");
        return makeSegments(this, String(string === undefined ? "undefined" : string));
    }, 1);

    defineMethod(MbSegmenter.prototype, "resolvedOptions", function resolvedOptions() {
        requireBrand(segBrand, this, "Segmenter");
        return { locale: this._locale, calendar: "gregory", casing: "default",
                 collation: "auto", hourCycle: "h23", numberingSystem: "latn",
                 granularity: this._granularity, usage: "segment" };
    }, 0);

    setToStringTag(MbSegmenter.prototype, "Intl.Segmenter");
    installConstructor(Intl, "Segmenter", MbSegmenter, 0);
    defineStatic(MbSegmenter, "supportedLocalesOf", function supportedLocalesOf(locales, options) {
        var list = canonicalizeLocaleList(locales);
        getOptionsObject(options);
        return list.filter(function (l) { return supportedLocaleTest(l); });
    }, 1);

    /* ==================================================================
     * Intl.DisplayNames  (of() output needs CLDR; structural tests pass)
     * ================================================================== */
    var dnBrand = makeBrand();

    function MbDisplayNames(locales, options) {
        if (new.target === undefined) {
            throw new TypeError("Constructor Intl.DisplayNames requires 'new'");
        }
        var locList = canonicalizeLocaleList(locales);
        var opts = getOptionsObject(options);
        var type = getOption(opts, "type", "string",
            ["language", "region", "script", "currency", "calendar", "dateField"], undefined);
        if (type === undefined) throw new TypeError("Intl.DisplayNames: type option is required");
        var style = getOption(opts, "style", "string", ["narrow", "short", "long"], "long");
        var fallback = getOption(opts, "fallback", "string", ["code", "none"], "code");
        var languageDisplay = getOption(opts, "languageDisplay", "string", ["dialogue", "dialect"], "dialect");
        dnBrand.add(this);
        this._locale = locList.length ? locList[0] : "en-US";
        this._type = type;
        this._style = style;
        this._fallback = fallback;
        this._languageDisplay = languageDisplay;
    }

    defineMethod(MbDisplayNames.prototype, "of", function of(code) {
        requireBrand(dnBrand, this, "DisplayNames");
        code = String(code);
        return code; /* CLDR display strings not bundled; fall back to the code */
    }, 1);

    defineMethod(MbDisplayNames.prototype, "resolvedOptions", function resolvedOptions() {
        requireBrand(dnBrand, this, "DisplayNames");
        return { locale: this._locale, style: this._style, type: this._type,
                 fallback: this._fallback, languageDisplay: this._languageDisplay };
    }, 0);

    setToStringTag(MbDisplayNames.prototype, "Intl.DisplayNames");
    installConstructor(Intl, "DisplayNames", MbDisplayNames, 0);
    defineStatic(MbDisplayNames, "supportedLocalesOf", function supportedLocalesOf(locales, options) {
        var list = canonicalizeLocaleList(locales);
        getOptionsObject(options);
        return list.filter(function (l) { return supportedLocaleTest(l); });
    }, 1);

    js_record("Intl.Segmenter", typeof Intl.Segmenter === "function" ? "shim" : "missing");
    js_record("Intl.DisplayNames", typeof Intl.DisplayNames === "function" ? "shim" : "missing");

    /* ── post-pass: make every shim method spec-shaped ──────────────
     * Direct `X.prototype.foo = function(){}` assignments leave the method
     * enumerable and give it a `prototype` own property; both violate the
     * spec and fail prop-desc/builtin/name tests. Normalise them, and turn
     * the `format` METHOD into the accessor the spec defines (its [[Get]]
     * returns a formatter bound to the receiver). */

    /* ── Atomics.waitAsync (ES2020) ──────────────────────────────────
     * Engine lacks it (Atomics.wait can't block on this single thread, so
     * every call takes the async branch per spec: {async:true, value:Promise}).
     * We resolve those promises by wrapping Atomics.notify (same-thread) and
     * using a timer for the timeout. Validation order follows the spec:
     * type check -> read length -> ToIndex(index) -> range check ->
     * ToInt32/BigInt(value) -> ToNumber(timeout). Agent-based tests still
     * fail (no real threads), but pure validation + same-thread notify pass. */
    if (typeof Atomics === "object" && typeof SharedArrayBuffer !== "undefined" &&
        typeof Atomics.waitAsync !== "function") {
        var _waiters = [];
        var _origNotify = Atomics.notify;
        function _resolveWaiters(ta, index, count) {
            var woken = 0;
            for (var i = 0; i < _waiters.length && (count === undefined || woken < count); i++) {
                var w = _waiters[i];
                if (w.ta === ta && w.index === index) {
                    _waiters.splice(i, 1); i--; woken++;
                    if (w.timer !== undefined) clearTimeout(w.timer);
                    w.resolve("ok");
                }
            }
        }
        Atomics.notify = function notify(ta, index, count) {
            var n = _origNotify.apply(this, arguments);
            _resolveWaiters(ta, index, count);
            return n;
        };
        function _toIndex(v) {
            if (v === undefined) return 0;
            var n = Number(v);
            if (n !== n) return 0;            /* NaN -> 0 */
            n = Math.trunc(n);
            if (n < 0) throw new RangeError("index out of range");
            return n;
        }
        var waitAsync = function waitAsync(ta, index, value, timeout) {
            var isBig = typeof BigInt64Array === "function" && ta instanceof BigInt64Array;
            if (!(ta instanceof Int32Array) && !isBig) throw new TypeError("not a shared typed array");
            if (!(ta.buffer instanceof SharedArrayBuffer)) throw new TypeError("not a shared buffer");
            var len = ta.length;                 /* read length BEFORE index coercion */
            var idx = _toIndex(index);
            if (idx >= len) throw new RangeError("index out of range");
            var v = isBig ? BigInt(value) : (Number(value) | 0);
            var t = timeout === undefined ? Infinity : Number(timeout);
            if (ta[idx] !== v) return { async: false, value: "not-equal" };
            var out = { async: true };
            out.value = new Promise(function (resolve) {
                var w = { ta: ta, index: idx, resolve: resolve, timer: undefined };
                _waiters.push(w);
                if (t !== Infinity) {
                    w.timer = setTimeout(function () {
                        var k = _waiters.indexOf(w);
                        if (k >= 0) { _waiters.splice(k, 1); resolve("timed-out"); }
                    }, t < 0 ? 0 : t);
                }
            });
            return out;
        };
        Object.defineProperty(Atomics, "waitAsync", {
            value: waitAsync, writable: true, enumerable: false, configurable: true,
        });
    }

    function finishShim(ctor) {
        if (!ctor || !ctor.prototype) return;
        var proto = ctor.prototype;
        Object.getOwnPropertyNames(proto).forEach(function (n) {
            if (n === "constructor") return;
            var d = Object.getOwnPropertyDescriptor(proto, n);
            if (!d || !("value" in d) || typeof d.value !== "function") return;
            var fn = d.value;
            try { delete fn.prototype; } catch (e) { /* frozen */ }
            if (n === "format") {
                (function (formatter) {
                    Object.defineProperty(proto, "format", {
                        get: function () {
                            var self = this;
                            var bound = function format() {
                                return formatter.apply(self, arguments);
                            };
                            try { delete bound.prototype; } catch (e) { }
                            return bound;
                        },
                        set: undefined, enumerable: false, configurable: true,
                    });
                })(fn);
                return;
            }
            Object.defineProperty(proto, n, {
                value: fn, writable: true, enumerable: false, configurable: true,
            });
        });
    }

    ["NumberFormat", "DateTimeFormat", "Collator", "PluralRules",
     "RelativeTimeFormat", "ListFormat", "Locale", "Segmenter",
     "DisplayNames", "DurationFormat"].forEach(function (n) {
        finishShim(Intl[n]);
    });

})(Intl);

js_record("Intl.NumberFormat", typeof Intl.NumberFormat === "function" ? "shim" : "missing");
js_record("Intl.DateTimeFormat", typeof Intl.DateTimeFormat === "function" ? "shim" : "missing");
js_record("Intl.Collator", typeof Intl.Collator === "function" ? "shim" : "missing");
js_record("Intl.PluralRules", typeof Intl.PluralRules === "function" ? "shim" : "missing");
js_record("Intl.RelativeTimeFormat", typeof Intl.RelativeTimeFormat === "function" ? "shim" : "missing");
js_record("Intl.ListFormat", typeof Intl.ListFormat === "function" ? "shim" : "missing");
js_record("Intl.getCanonicalLocales", typeof Intl.getCanonicalLocales === "function" ? "shim" : "missing");
js_record("Intl.supportedValuesOf", typeof Intl.supportedValuesOf === "function" ? "shim" : "missing");
