
var ok = {}, e0 = null;
try {
  ok.Temporal = typeof Temporal;
  ok.PlainDate = typeof Temporal.PlainDate;
  var d = Temporal.PlainDate.from("2026-11-04");
  ok.iso = d.toString();
  ok.plus = d.add({months: 2}).toString();
  var i = Temporal.Instant.from("2026-11-04T12:00:00Z");
  ok.instant = i.epochNanoseconds.toString().slice(0,6);
  var dtp = Temporal.PlainDateTime.from("2026-11-04T09:30:00");
  ok.dur = Temporal.Duration.from({hours:1, minutes:2}).toString();
  ok.calendar = d.calendar.id;
  var zdt = Temporal.ZonedDateTime.from("2026-11-04T09:00:00+01:00[Europe/Paris]");
  ok.zdt = zdt.hour;
  ok.roundtrip = Temporal.PlainYearMonth.from("2026-05").toString();
} catch(e){ e0 = e && (e.message); }
print(JSON.stringify(ok));
if (e0) print("ERR:", e0);
