/**
 * patch_temporal.js — Minimal Temporal API polyfill (ES2026/ES2027)
 *
 * Temporal is a Stage 4 proposal expected in ES2027. This is a minimal but
 * functional polyfill covering the most commonly used types:
 *   - Temporal.Instant (epoch nanoseconds)
 *   - Temporal.PlainDate (year, month, day)
 *   - Temporal.PlainTime (hour, minute, second, ms, us, ns)
 *   - Temporal.PlainDateTime (date + time)
 *   - Temporal.ZonedDateTime (date + time + timezone)
 *   - Temporal.Duration (years, months, days, hours, min, sec, ms, us, ns)
 *   - Temporal.Now (system clock access)
 *
 * Self-skipping: installs only if Temporal is not already provided by the engine.
 *
 * Copyright (c) 2024-2026 Berry Lab Foundation — libmochiberry project
 */

"use strict";

if (typeof Temporal !== "undefined") {
    /* Engine already provides Temporal — skip polyfill entirely. */
} else {

const js_registry = globalThis.__mbjs_patches ||
    (globalThis.__mbjs_patches = {});

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

function js_to_int(v, name) {
    const n = Number(v);
    if (!Number.isInteger(n))
        throw new RangeError(name + " must be an integer");
    return n;
}

function js_to_nonneg(v, name) {
    const n = js_to_int(v, name);
    if (n < 0)
        throw new RangeError(name + " must not be negative");
    return n;
}

function js_to_string(v) {
    return String(v);
}

function js_pad2(n) { return String(n).padStart(2, "0"); }
function js_pad9(n) { return String(n).padStart(9, "0"); }
function js_pad3(n) { return String(n).padStart(3, "0"); }

/* ValidatePlainDate components per ISO 8601 calendar */
function js_validate_date(y, m, d) {
    if (m < 1 || m > 12) throw new RangeError("invalid month");
    if (d < 1 || d > 31) throw new RangeError("invalid day");
    /* Basic month-day validation */
    const days_in_month = [0,31,28,31,30,31,30,31,31,30,31,30,31];
    const leap = (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0;
    if (leap) days_in_month[2] = 29;
    if (d > days_in_month[m])
        throw new RangeError("invalid day for month");
}

/* ================================================================== */
/* Temporal.Duration                                                    */
/* ================================================================== */

class TemporalDuration {
    constructor(years = 0, months = 0, days = 0, hours = 0, minutes = 0,
                seconds = 0, milliseconds = 0, microseconds = 0, nanoseconds = 0) {
        this.years = js_to_int(years, "years");
        this.months = js_to_int(months, "months");
        this.days = js_to_int(days, "days");
        this.hours = js_to_int(hours, "hours");
        this.minutes = js_to_int(minutes, "minutes");
        this.seconds = js_to_int(seconds, "seconds");
        this.milliseconds = js_to_int(milliseconds, "milliseconds");
        this.microseconds = js_to_int(microseconds, "microseconds");
        this.nanoseconds = js_to_int(nanoseconds, "nanoseconds");
    }

    get sign() {
        for (const f of ["years","months","days","hours","minutes","seconds",
                         "milliseconds","microseconds","nanoseconds"]) {
            if (this[f] < 0) return -1;
            if (this[f] > 0) return 1;
        }
        return 0;
    }

    get blank() { return this.sign === 0; }

    with({ years, months, days, hours, minutes, seconds, milliseconds, microseconds, nanoseconds } = {}) {
        return new TemporalDuration(
            years !== undefined ? years : this.years,
            months !== undefined ? months : this.months,
            days !== undefined ? days : this.days,
            hours !== undefined ? hours : this.hours,
            minutes !== undefined ? minutes : this.minutes,
            seconds !== undefined ? seconds : this.seconds,
            milliseconds !== undefined ? milliseconds : this.milliseconds,
            microseconds !== undefined ? microseconds : this.microseconds,
            nanoseconds !== undefined ? nanoseconds : this.nanoseconds
        );
    }

    negate() {
        return new TemporalDuration(
            -this.years, -this.months, -this.days,
            -this.hours, -this.minutes, -this.seconds,
            -this.milliseconds, -this.microseconds, -this.nanoseconds
        );
    }

    abs() {
        const s = (v) => v < 0 ? -v : v;
        return new TemporalDuration(
            s(this.years), s(this.months), s(this.days),
            s(this.hours), s(this.minutes), s(this.seconds),
            s(this.milliseconds), s(this.microseconds), s(this.nanoseconds)
        );
    }

    add(other) {
        return new TemporalDuration(
            this.years + other.years, this.months + other.months,
            this.days + other.days, this.hours + other.hours,
            this.minutes + other.minutes, this.seconds + other.seconds,
            this.milliseconds + other.milliseconds,
            this.microseconds + other.microseconds,
            this.nanoseconds + other.nanoseconds
        );
    }

    subtract(other) {
        return new TemporalDuration(
            this.years - other.years, this.months - other.months,
            this.days - other.days, this.hours - other.hours,
            this.minutes - other.minutes, this.seconds - other.seconds,
            this.milliseconds - other.milliseconds,
            this.microseconds - other.microseconds,
            this.nanoseconds - other.nanoseconds
        );
    }

    total(totalOf) {
        /* Real Temporal accepts either a string unit or a { unit } bag. */
        let unit;
        if (typeof totalOf === "string")
            unit = totalOf;
        else if (totalOf && typeof totalOf === "object")
            unit = totalOf.unit;
        if (typeof unit !== "string")
            throw new TypeError("Temporal.Duration.total: missing unit");
        const fields = ["years","months","days","hours","minutes","seconds",
                        "milliseconds","microseconds","nanoseconds"];
        const idx = fields.indexOf(unit);
        if (idx < 0) throw new RangeError("invalid unit: " + unit);
        /* months/years are calendar-dependent; return 0 for now */
        if (idx < 2) return 0;
        /* Accumulate every unit from days down to nanoseconds as a
           nanosecond total, then divide by the target unit's size. */
        let totalNS = 0;
        totalNS += this.days * 86400e9;
        totalNS += this.hours * 3600e9;
        totalNS += this.minutes * 60e9;
        totalNS += this.seconds * 1e9;
        totalNS += this.milliseconds * 1e6;
        totalNS += this.microseconds * 1e3;
        totalNS += this.nanoseconds;
        const unitNS = idx === 2 ? 86400e9
            : idx === 3 ? 3600e9
            : idx === 4 ? 60e9
            : idx === 5 ? 1e9
            : idx === 6 ? 1e6
            : idx === 7 ? 1e3
            : 1;
        return totalNS / unitNS;
    }

    toString() {
        let s = "P";
        if (this.years) s += this.years + "Y";
        if (this.months) s += this.months + "M";
        if (this.days) s += this.days + "D";
        const t = (this.hours || this.minutes || this.seconds ||
                   this.milliseconds || this.microseconds || this.nanoseconds);
        if (t) {
            s += "T";
            if (this.hours) s += this.hours + "H";
            if (this.minutes) s += this.minutes + "M";
            if (this.seconds || this.milliseconds || this.microseconds || this.nanoseconds) {
                let sec = String(this.seconds);
                if (this.milliseconds || this.microseconds || this.nanoseconds) {
                    sec += "." + js_pad2(this.milliseconds).slice(0, 3);
                    if (this.microseconds || this.nanoseconds)
                        sec += js_pad3(this.microseconds).slice(0, 3);
                    if (this.nanoseconds)
                        sec += js_pad9(this.nanoseconds).slice(0, 3);
                }
                s += sec + "S";
            }
        }
        return s === "P" ? "PT0S" : s;
    }

    toJSON() { return this.toString(); }
    toLocaleString() { return this.toString(); }

    static from(item) {
        if (item instanceof TemporalDuration)
            return item;
        if (typeof item === "string") {
            const m = item.match(/^P(?:(\d+)Y)?(?:(\d+)M)?(?:(\d+)D)?(?:T(?:(\d+(?:\.\d+)?)H)?(?:(\d+(?:\.\d+)?)M)?(?:(\d+(?:\.\d+)?)S)?)?$/);
            if (!m)
                throw new RangeError("Temporal.Duration.from: invalid string: " + item);
            const num = (s) => (s === undefined ? 0 : parseFloat(s));
            const h = num(m[4]), mi = num(m[5]), s = num(m[6]);
            return new TemporalDuration(
                num(m[1]), num(m[2]), num(m[3]),
                Math.floor(h), Math.floor(mi), Math.floor(s),
                Math.round((h - Math.floor(h)) * 1000) +
                Math.round((mi - Math.floor(mi)) * 1000) +
                Math.round((s - Math.floor(s)) * 1000),
                0, 0);
        }
        if (item && typeof item === "object") {
            const g = (k) => (item[k] === undefined ? 0 : item[k]);
            return new TemporalDuration(
                g("years"), g("months"), g("days"), g("hours"), g("minutes"),
                g("seconds"), g("milliseconds"), g("microseconds"),
                g("nanoseconds"));
        }
        throw new TypeError("Temporal.Duration.from: invalid argument");
    }
}

/* ================================================================== */
/* Temporal.PlainDate                                                   */
/* ================================================================== */

class TemporalPlainDate {
    constructor(isoYear, isoMonth, isoDay, calendar = "iso8601") {
        this._year = js_to_nonneg(isoYear, "isoYear");
        this._month = js_to_nonneg(isoMonth, "isoMonth");
        this._day = js_to_nonneg(isoDay, "isoDay");
        this._calendar = calendar;
        js_validate_date(this._year, this._month, this._day);
    }

    get year() { return this._year; }
    get month() { return this._month; }
    get day() { return this._day; }
    get calendarId() { return this._calendar; }

    with({ year, month, day } = {}) {
        return new TemporalPlainDate(
            year !== undefined ? year : this._year,
            month !== undefined ? month : this._month,
            day !== undefined ? day : this._day,
            this._calendar
        );
    }

    add(duration) {
        let dy = (duration.years || 0);
        let dm = (duration.months || 0);
        let dd = (duration.days || 0);
        if (!Number.isFinite(dy) || !Number.isFinite(dm) || !Number.isFinite(dd)) {
            throw new RangeError("invalid duration");
        }
        dy = Math.trunc(dy); dm = Math.trunc(dm); dd = Math.trunc(dd);
        let y = this._year + dy;
        let m = this._month + dm;
        let d = this._day + dd;
        /* Normalize months */
        while (m > 12) { y++; m -= 12; }
        while (m < 1) { y--; m += 12; }
        js_validate_date(y, m, d);
        return new TemporalPlainDate(y, m, d, this._calendar);
    }

    subtract(duration) {
        const neg = { years: -(duration.years || 0),
                      months: -(duration.months || 0),
                      days: -(duration.days || 0) };
        return this.add(neg);
    }

    withPlainTime(time) {
        return new TemporalPlainDateTime(
            this._year, this._month, this._day,
            time.hour, time.minute, time.second,
            time.millisecond, time.microsecond, time.nanosecond,
            this._calendar
        );
    }

    equals(other) {
        return this._year === other._year &&
               this._month === other._month &&
               this._day === other._day;
    }

    toString() {
        return `${this._year}-${js_pad2(this._month)}-${js_pad2(this._day)}`;
    }

    toJSON() { return this.toString(); }
    toLocaleString() { return this.toString(); }

    valueOf() { throw new TypeError("cannot compare Temporal.PlainDate"); }

    static from(item) {
        if (item instanceof TemporalPlainDate)
            return item;
        if (typeof item === "string") {
            const m = item.match(/^([+-]\d{6}|\d{4})-(\d{2})-(\d{2})$/);
            if (!m)
                throw new RangeError("Temporal.PlainDate.from: invalid ISO string: " + item);
            return new TemporalPlainDate(Number(m[1]), Number(m[2]), Number(m[3]));
        }
        if (item && typeof item === "object") {
            return new TemporalPlainDate(
                Number(item.year),
                Number(item.month !== undefined ? item.month : item.monthCode),
                Number(item.day),
                item.calendar || "iso8601");
        }
        throw new TypeError("Temporal.PlainDate.from: invalid argument");
    }
}

/* ================================================================== */
/* Temporal.PlainTime                                                   */
/* ================================================================== */

class TemporalPlainTime {
    constructor(isoHour = 0, isoMinute = 0, isoSecond = 0,
                isoMillisecond = 0, isoMicrosecond = 0, isoNanosecond = 0) {
        this._hour = js_to_nonneg(isoHour, "isoHour");
        this._minute = js_to_nonneg(isoMinute, "isoMinute");
        this._second = js_to_nonneg(isoSecond, "isoSecond");
        this._millisecond = js_to_nonneg(isoMillisecond, "isoMillisecond");
        this._microsecond = js_to_nonneg(isoMicrosecond, "isoMicrosecond");
        this._nanosecond = js_to_nonneg(isoNanosecond, "isoNanosecond");
        if (this._hour > 23) throw new RangeError("hour out of range");
        if (this._minute > 59) throw new RangeError("minute out of range");
        if (this._second > 59) throw new RangeError("second out of range");
    }

    get hour() { return this._hour; }
    get minute() { return this._minute; }
    get second() { return this._second; }
    get millisecond() { return this._millisecond; }
    get microsecond() { return this._microsecond; }
    get nanosecond() { return this._nanosecond; }

    toString() {
        let s = `${js_pad2(this._hour)}:${js_pad2(this._minute)}:${js_pad2(this._second)}`;
        if (this._millisecond || this._microsecond || this._nanosecond) {
            s += "." + js_pad3(this._millisecond);
            if (this._microsecond || this._nanosecond)
                s += js_pad3(this._microsecond);
            if (this._nanosecond)
                s += js_pad3(this._nanosecond);
        }
        return s;
    }

    toJSON() { return this.toString(); }
    toLocaleString() { return this.toString(); }
    valueOf() { throw new TypeError("cannot compare Temporal.PlainTime"); }

    static from(item) {
        if (item instanceof TemporalPlainTime)
            return item;
        if (typeof item === "string") {
            const m = item.match(/^(\d{2}):(\d{2})(?::(\d{2})(?:\.(\d+))?)?$/);
            if (!m)
                throw new RangeError("Temporal.PlainTime.from: invalid ISO string: " + item);
            const frac = m[4] || "";
            const ms = parseInt((frac + "000").slice(0, 3), 10);
            const us = parseInt((frac + "000000").slice(3, 6), 10);
            const ns = parseInt((frac + "000000000").slice(6, 9), 10);
            return new TemporalPlainTime(
                Number(m[1]), Number(m[2]),
                m[3] === undefined ? 0 : Number(m[3]), ms, us, ns);
        }
        if (item && typeof item === "object") {
            const g = (k, d) => (item[k] === undefined ? d : Number(item[k]));
            return new TemporalPlainTime(g("hour", 0), g("minute", 0),
                g("second", 0), g("millisecond", 0), g("microsecond", 0),
                g("nanosecond", 0));
        }
        throw new TypeError("Temporal.PlainTime.from: invalid argument");
    }
}

/* ================================================================== */
/* Temporal.PlainDateTime                                              */
/* ================================================================== */

class TemporalPlainDateTime {
    constructor(isoYear, isoMonth, isoDay, isoHour = 0, isoMinute = 0,
                isoSecond = 0, isoMillisecond = 0, isoMicrosecond = 0,
                isoNanosecond = 0, calendar = "iso8601") {
        this._year = js_to_nonneg(isoYear, "isoYear");
        this._month = js_to_nonneg(isoMonth, "isoMonth");
        this._day = js_to_nonneg(isoDay, "isoDay");
        this._hour = js_to_nonneg(isoHour, "isoHour");
        this._minute = js_to_nonneg(isoMinute, "isoMinute");
        this._second = js_to_nonneg(isoSecond, "isoSecond");
        this._millisecond = js_to_nonneg(isoMillisecond, "isoMillisecond");
        this._microsecond = js_to_nonneg(isoMicrosecond, "isoMicrosecond");
        this._nanosecond = js_to_nonneg(isoNanosecond, "isoNanosecond");
        this._calendar = calendar;
        js_validate_date(this._year, this._month, this._day);
    }

    get year() { return this._year; }
    get month() { return this._month; }
    get day() { return this._day; }
    get hour() { return this._hour; }
    get minute() { return this._minute; }
    get second() { return this._second; }
    get millisecond() { return this._millisecond; }
    get microsecond() { return this._microsecond; }
    get nanosecond() { return this._nanosecond; }
    get calendarId() { return this._calendar; }

    get plainDate() {
        return new TemporalPlainDate(this._year, this._month, this._day, this._calendar);
    }

    get plainTime() {
        return new TemporalPlainTime(this._hour, this._minute, this._second,
            this._millisecond, this._microsecond, this._nanosecond);
    }

    with({ year, month, day, hour, minute, second, millisecond, microsecond, nanosecond } = {}) {
        return new TemporalPlainDateTime(
            year !== undefined ? year : this._year,
            month !== undefined ? month : this._month,
            day !== undefined ? day : this._day,
            hour !== undefined ? hour : this._hour,
            minute !== undefined ? minute : this._minute,
            second !== undefined ? second : this._second,
            millisecond !== undefined ? millisecond : this._millisecond,
            microsecond !== undefined ? microsecond : this._microsecond,
            nanosecond !== undefined ? nanosecond : this._nanosecond,
            this._calendar
        );
    }

    add(duration) {
        const newDate = this.plainDate.add(duration);
        let nh = this._hour + duration.hours;
        let nm = this._minute + duration.minutes;
        let ns = this._second + duration.seconds;
        let nms = this._millisecond + duration.milliseconds;
        let nus = this._microsecond + duration.microseconds;
        let nns = this._nanosecond + duration.nanoseconds;
        /* Carry propagation */
        nns += Math.floor(nus / 1000); nus = ((nus % 1000) + 1000) % 1000;
        nus += Math.floor(nms / 1000); nms = ((nms % 1000) + 1000) % 1000;
        nms += Math.floor(ns / 1000); ns = ((ns % 1000) + 1000) % 1000;
        ns += Math.floor(nm / 60); nm = ((nm % 60) + 60) % 60;
        nm += Math.floor(nh / 60); nh = ((nh % 60) + 60) % 60;
        const extra_days = Math.floor(nh / 24); nh = ((nh % 24) + 24) % 24;
        let finalDate = newDate;
        if (extra_days !== 0) {
            finalDate = newDate.add(new TemporalDuration(0, 0, extra_days));
        }
        return new TemporalPlainDateTime(
            finalDate.year, finalDate.month, finalDate.day,
            nh, nm, ns, nms, nus, nns, this._calendar
        );
    }

    equals(other) {
        return this._year === other._year && this._month === other._month &&
               this._day === other._day && this._hour === other._hour &&
               this._minute === other._minute && this._second === other._second &&
               this._millisecond === other._millisecond;
    }

    toString() {
        return `${this._year}-${js_pad2(this._month)}-${js_pad2(this._day)}T` +
               `${js_pad2(this._hour)}:${js_pad2(this._minute)}:${js_pad2(this._second)}` +
               (this._millisecond || this._microsecond || this._nanosecond
                   ? `.${js_pad3(this._millisecond)}${js_pad3(this._microsecond)}${js_pad3(this._nanosecond)}`
                   : "");
    }

    toJSON() { return this.toString(); }
    toLocaleString() { return this.toString(); }
    valueOf() { throw new TypeError("cannot compare Temporal.PlainDateTime"); }

    static from(item) {
        if (item instanceof TemporalPlainDateTime)
            return item;
        if (typeof item === "string") {
            const m = item.match(
                /^([+-]\d{6}|\d{4})-(\d{2})-(\d{2})(?:[T ](\d{2}):(\d{2})(?::(\d{2})(?:\.(\d+))?)?)?$/
            );
            if (!m)
                throw new RangeError("Temporal.PlainDateTime.from: invalid ISO string: " + item);
            const frac = m[7] || "";
            const ms = parseInt((frac + "000").slice(0, 3), 10);
            const us = parseInt((frac + "000000").slice(3, 6), 10);
            const ns = parseInt((frac + "000000000").slice(6, 9), 10);
            return new TemporalPlainDateTime(
                Number(m[1]), Number(m[2]), Number(m[3]),
                m[4] === undefined ? 0 : Number(m[4]),
                m[5] === undefined ? 0 : Number(m[5]),
                m[6] === undefined ? 0 : Number(m[6]),
                ms, us, ns);
        }
        if (item && typeof item === "object") {
            const g = (k, d) => (item[k] === undefined ? d : Number(item[k]));
            return new TemporalPlainDateTime(g("year", 0), g("month", 1),
                g("day", 1), g("hour", 0), g("minute", 0), g("second", 0),
                g("millisecond", 0), g("microsecond", 0),
                g("nanosecond", 0));
        }
        throw new TypeError("Temporal.PlainDateTime.from: invalid argument");
    }
}

/* ================================================================== */
/* Temporal.ZonedDateTime                                              */
/* ================================================================== */

class TemporalZonedDateTime {
    constructor(epochNanoseconds, timeZone = "UTC", calendar = "iso8601") {
        this._epochNanoseconds = typeof epochNanoseconds === "bigint"
            ? epochNanoseconds : BigInt(Math.floor(Number(epochNanoseconds)));
        this._timeZone = timeZone;
        this._calendar = calendar;
    }

    get epochMilliseconds() { return Number(this._epochNanoseconds / 1000000n); }
    get epochNanoseconds() { return this._epochNanoseconds; }
    get timeZoneId() { return this._timeZone; }
    get calendarId() { return this._calendar; }

    /* Convert to UTC for basic display (no real timezone offset handling) */
    get year() { return this.toPlainDateTime().year; }
    get month() { return this.toPlainDateTime().month; }
    get day() { return this.toPlainDateTime().day; }
    get hour() { return this.toPlainDateTime().hour; }
    get minute() { return this.toPlainDateTime().minute; }
    get second() { return this.toPlainDateTime().second; }
    get millisecond() { return this.toPlainDateTime().millisecond; }

    toPlainDateTime() {
        const ms = this.epochMilliseconds;
        const d = new Date(ms);
        return new TemporalPlainDateTime(
            d.getUTCFullYear(), d.getUTCMonth() + 1, d.getUTCDate(),
            d.getUTCHours(), d.getUTCMinutes(), d.getUTCSeconds(),
            d.getUTCMilliseconds(), 0, 0, this._calendar
        );
    }

    toPlainDate() {
        return this.toPlainDateTime().plainDate;
    }

    toPlainTime() {
        return this.toPlainDateTime().plainTime;
    }

    equals(other) {
        return this._epochNanoseconds === other._epochNanoseconds;
    }

    toString() {
        const pdt = this.toPlainDateTime();
        const tz = this._timeZone === "UTC" ? "Z" : `[${this._timeZone}]`;
        return pdt.toString() + tz;
    }

    toJSON() { return this.toString(); }
    toLocaleString() { return this.toString(); }
    valueOf() { throw new TypeError("cannot compare Temporal.ZonedDateTime"); }
}

/* ================================================================== */
/* Temporal.Instant                                                    */
/* ================================================================== */

class TemporalInstant {
    constructor(epochNanoseconds) {
        this._epochNanoseconds = typeof epochNanoseconds === "bigint"
            ? epochNanoseconds : BigInt(Math.floor(Number(epochNanoseconds)));
    }

    get epochMilliseconds() { return Number(this._epochNanoseconds / 1000000n); }
    get epochNanoseconds() { return this._epochNanoseconds; }

    add(duration) {
        const ns = this._epochNanoseconds +
            BigInt(duration.seconds) * 1000000000n +
            BigInt(duration.milliseconds) * 1000000n +
            BigInt(duration.microseconds) * 1000n +
            BigInt(duration.nanoseconds);
        return new TemporalInstant(ns);
    }

    subtract(duration) { return this.add(duration.negate()); }

    equals(other) { return this._epochNanoseconds === other._epochNanoseconds; }

    toZonedDateTime({ timeZone = "UTC", calendar = "iso8601" } = {}) {
        return new TemporalZonedDateTime(this._epochNanoseconds, timeZone, calendar);
    }

    toPlainDateTime({ timeZone = "UTC" } = {}) {
        return this.toZonedDateTime({ timeZone }).toPlainDateTime();
    }

    toString() {
        return this.toZonedDateTime().toString();
    }

    toJSON() { return this.toString(); }
    toLocaleString() { return this.toString(); }
        valueOf() { throw new TypeError("cannot compare Temporal.Instant"); }

    static from(item) {
        if (item instanceof TemporalInstant)
            return item;
        if (typeof item === "string") {
            const m = item.match(
                /^(\d{4})-(\d{2})-(\d{2})[T ](\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?(?:Z|([+-]\d{2}):?(\d{2}))?$/
            );
            if (!m)
                throw new RangeError("Temporal.Instant.from: invalid ISO string: " + item);
            const frac = m[7] || "";
            const ms = parseInt((frac + "000").slice(0, 3), 10);
            const dt = Date.UTC(Number(m[1]), Number(m[2]) - 1, Number(m[3]),
                Number(m[4]), Number(m[5]), Number(m[6]), ms);
            let offsetMin = 0;
            if (m[8] !== undefined) {
                const oh = Number(m[8]), om = Number(m[9] || "0");
                offsetMin = (oh >= 0 ? 1 : -1) * (Math.abs(oh) * 60 + om);
            }
            const epochMs = dt - offsetMin * 60000;
            return new TemporalInstant(BigInt(epochMs) * 1000000n);
        }
        if (item && typeof item === "object" && item.epochNanoseconds !== undefined)
            return new TemporalInstant(item.epochNanoseconds);
        if (item && typeof item === "object" && item.epochMilliseconds !== undefined)
            return new TemporalInstant(BigInt(item.epochMilliseconds) * 1000000n);
        throw new TypeError("Temporal.Instant.from: invalid argument");
    }
}

/* ================================================================== */
/* Temporal.Now                                                        */
/* ================================================================== */

const TemporalNow = {
    instant() {
        return new TemporalInstant(BigInt(Date.now()) * 1000000n);
    },
    zonedDateTimeISO(timeZone = "UTC") {
        return this.instant().toZonedDateTime({ timeZone });
    },
    plainDateTimeISO(timeZone = "UTC") {
        return this.instant().toPlainDateTime({ timeZone });
    },
    plainDateISO() {
        return this.plainDateTimeISO().plainDate;
    },
    plainTimeISO() {
        return this.plainDateTimeISO().plainTime;
    },
    timeZoneId() {
        /* Best-effort: return "UTC" if we can't detect the local zone */
        try {
            return Intl.DateTimeFormat().resolvedOptions().timeZone;
        } catch (e) {
            return "UTC";
        }
    }
};

/* ================================================================== */
/* Temporal.compare and Temporal.from                                  */
/* ================================================================== */

function temporalCompare(a, b) {
    if (a instanceof TemporalInstant && b instanceof TemporalInstant) {
        if (a._epochNanoseconds < b._epochNanoseconds) return -1;
        if (a._epochNanoseconds > b._epochNanoseconds) return 1;
        return 0;
    }
    throw new TypeError("Temporal.compare: arguments must be the same type");
}

function temporalFrom(item) {
    if (typeof item === "string") {
        /* ISO 8601 parsing (basic subset) */
        const dtMatch = item.match(
            /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d{3})(\d{3})(\d{3}))?(?:Z|([+-]\d{2}):?(\d{2}))?$/
        );
        if (dtMatch) {
            return new TemporalPlainDateTime(
                Number(dtMatch[1]), Number(dtMatch[2]), Number(dtMatch[3]),
                Number(dtMatch[4]), Number(dtMatch[5]), Number(dtMatch[6]),
                Number(dtMatch[7] || "0"), Number(dtMatch[8] || "0"),
                Number(dtMatch[9] || "0")
            );
        }
        const dateMatch = item.match(/^(\d{4})-(\d{2})-(\d{2})$/);
        if (dateMatch) {
            return new TemporalPlainDate(
                Number(dateMatch[1]), Number(dateMatch[2]), Number(dateMatch[3])
            );
        }
        throw new RangeError("Temporal.from: invalid ISO string: " + item);
    }
    if (item instanceof TemporalPlainDate ||
        item instanceof TemporalPlainTime ||
        item instanceof TemporalPlainDateTime ||
        item instanceof TemporalZonedDateTime ||
        item instanceof TemporalInstant ||
        item instanceof TemporalDuration) {
        return item;
    }
    throw new TypeError("Temporal.from: invalid argument");
}

/* ================================================================== */
/* Install                                                             */
/* ================================================================== */

globalThis.Temporal = {
    PlainDate: TemporalPlainDate,
    PlainTime: TemporalPlainTime,
    PlainDateTime: TemporalPlainDateTime,
    ZonedDateTime: TemporalZonedDateTime,
    Instant: TemporalInstant,
    Duration: TemporalDuration,
    Now: TemporalNow,
    compare: temporalCompare,
    from: temporalFrom,
};

js_registry["Temporal"] = "patched (minimal polyfill, ES2026/ES2027)";

} /* end Temporal polyfill */
