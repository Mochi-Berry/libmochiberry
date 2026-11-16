import "./test262_prelude.js";

print("Temporal=" + typeof Temporal);
print("PlainDate=" + typeof Temporal.PlainDate);
var instance = Temporal.PlainDate.from({ year: 2000, month: 5, day: 2 });
print("from ok: " + instance.toString());
var fields = ["years","months","weeks","days","hours","minutes","seconds","milliseconds","microseconds","nanoseconds"];
for (var i = 0; i < fields.length; i++) {
  var f = fields[i];
  var bag = {};
  bag[f] = Infinity;
  try {
    instance.add(bag, { overflow: "constrain" });
    print("  " + f + ": no-throw");
  } catch (e) {
    print("  " + f + ": threw " + e.constructor.name + ": " + e.message);
  }
}
print("done");
