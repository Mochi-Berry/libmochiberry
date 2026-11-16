/* Tests for Temporal API polyfill (ES2026/ES2027) */
"use strict";

let passed = 0;
let failed = 0;
function assert(cond, msg) {
    if (cond) { passed++; }
    else { failed++; print("FAIL: " + msg); }
}

/* 1. Temporal.PlainDate */
const d = new Temporal.PlainDate(2026, 8, 18);
assert(d.year === 2026, "PlainDate year");
assert(d.month === 8, "PlainDate month");
assert(d.day === 18, "PlainDate day");
assert(d.toString() === "2026-08-18", "PlainDate toString");
assert(d.calendarId === "iso8601", "PlainDate calendarId");

/* 2. PlainDate.with */
const d2 = d.with({ day: 25 });
assert(d2.toString() === "2026-08-25", "PlainDate.with");

/* 3. PlainDate.add */
const d3 = d.add(new Temporal.Duration(0, 1, 0, 15));
assert(d3.toString() === "2026-10-03", "PlainDate.add month + day");
const d3b = d.add(new Temporal.Duration(0, 0, 0, 15));
assert(d3b.toString() === "2026-09-02", "PlainDate.add day (overflow)");

/* 4. PlainDate.subtract */
const d4 = d.subtract(new Temporal.Duration(0, 0, 0, 5));
assert(d4.toString() === "2026-08-13", "PlainDate.subtract");

/* 5. PlainDate.equals */
assert(d.equals(new Temporal.PlainDate(2026, 8, 18)), "PlainDate.equals same");
assert(!d.equals(d2), "PlainDate.equals different");

/* 6. Temporal.PlainTime */
const t = new Temporal.PlainTime(14, 30, 45, 123, 456, 789);
assert(t.hour === 14, "PlainTime hour");
assert(t.minute === 30, "PlainTime minute");
assert(t.second === 45, "PlainTime second");
assert(t.millisecond === 123, "PlainTime millisecond");
assert(t.microsecond === 456, "PlainTime microsecond");
assert(t.nanosecond === 789, "PlainTime nanosecond");

/* 7. PlainTime.toString */
const t2 = new Temporal.PlainTime(9, 5, 3);
assert(t2.toString() === "09:05:03", "PlainTime toString simple");

/* 8. Temporal.PlainDateTime */
const dt = new Temporal.PlainDateTime(2026, 8, 18, 14, 30, 45);
assert(dt.year === 2026, "PlainDateTime year");
assert(dt.hour === 14, "PlainDateTime hour");
assert(dt.toString() === "2026-08-18T14:30:45", "PlainDateTime toString");

/* 9. PlainDateTime.from PlainDate + PlainTime */
const dt2 = d.toPlainDateTime(t);
assert(dt2.year === 2026, "PlainDateTime from PlainDate+Time year");
assert(dt2.hour === 14, "PlainDateTime from PlainDate+Time hour");

/* 10. PlainDateTime.add */
const dt3 = dt.add(new Temporal.Duration(0, 0, 1, 2, 30));
assert(dt3.day === 28, "PlainDateTime add day overflow");
assert(dt3.hour === 20, "PlainDateTime add hours");

/* 11. Temporal.Duration */
const dur = new Temporal.Duration(1, 2, 3, 4, 5, 6, 7, 8, 9);
assert(dur.years === 1, "Duration years");
assert(dur.months === 2, "Duration months");
assert(dur.days === 4, "Duration days");
assert(dur.hours === 5, "Duration hours");
assert(dur.minutes === 6, "Duration minutes");
assert(dur.seconds === 7, "Duration seconds");
assert(dur.sign === 1, "Duration sign positive");

/* 12. Duration.negate */
const durNeg = dur.negate();
assert(durNeg.sign === -1, "Duration negate sign");
assert(durNeg.years === -1, "Duration negate years");

/* 13. Duration.abs */
const durAbs = durNeg.abs();
assert(durAbs.years === 1, "Duration abs years");
assert(durAbs.sign === 1, "Duration abs sign");

/* 14. Duration.add */
const dur2 = new Temporal.Duration(0, 0, 0, 0, 1);
const dur3 = new Temporal.Duration(0, 0, 0, 0, 5, 6).add(dur2);
assert(dur3.hours === 6, "Duration add hours");
assert(dur3.minutes === 6, "Duration add minutes");

/* 15. Duration.blank */
const durZero = new Temporal.Duration();
assert(durZero.blank === true, "Duration blank");
assert(dur.blank === false, "Duration non-blank");

/* 16. Duration.total */
const durT = new Temporal.Duration(0, 0, 0, 1, 5);
assert(durT.total({ unit: "hours" }) === 29, "Duration total hours");

/* 17. Temporal.Instant */
const inst = new Temporal.Instant(BigInt(1700000000000) * 1000000n);
assert(inst.epochMilliseconds === 1700000000000, "Instant epochMilliseconds");

/* 18. Instant.add */
const inst2 = inst.add(new Temporal.Duration(0, 0, 0, 0, 1));
assert(inst2.epochMilliseconds === 1700000000000 + 3600000, "Instant add");

/* 19. Temporal.ZonedDateTime */
const zdt = new Temporal.ZonedDateTime(BigInt(1700000000000) * 1000000n, "UTC");
assert(zdt.timeZoneId === "UTC", "ZonedDateTime timezone");
assert(zdt.epochMilliseconds === 1700000000000, "ZonedDateTime epoch");

/* 20. Temporal.Now */
const now = Temporal.Now.plainDateTimeISO();
assert(typeof now.year === "number", "Now plainDateTimeISO year");
assert(now.year >= 2024, "Now year reasonable");

/* 21. Temporal.from */
const fromDt = Temporal.PlainDateTime.from("2026-12-25T00:00:00");
assert(fromDt.year === 2026, "Temporal.from datetime year");
assert(fromDt.month === 12, "Temporal.from datetime month");

const fromDate = Temporal.PlainDate.from("2026-01-01");
assert(fromDate.year === 2026, "Temporal.from date year");

/* 22. PlainDateTime.with */
const dt4 = dt.with({ hour: 20 });
assert(dt4.hour === 20, "PlainDateTime.with hour");

/* 23. PlainDateTime.equals */
assert(dt.equals(new Temporal.PlainDateTime(2026, 8, 18, 14, 30, 45)),
    "PlainDateTime equals");

/* 24. ZonedDateTime.toPlainDateTime */
const zdtPdt = zdt.toPlainDateTime();
assert(typeof zdtPdt.hour === "number", "ZonedDateTime.toPlainDateTime");

/* Summary */
print("Temporal tests: " + passed + " passed, " + failed + " failed");
if (failed > 0) {
    print("SOME TESTS FAILED");
    throw new Error("Temporal tests failed: " + failed);
} else {
    print("ALL TEMPORAL TESTS PASSED");
}
