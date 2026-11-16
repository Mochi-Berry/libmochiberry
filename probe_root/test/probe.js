/*---
description: probe
features: [Temporal]
---*/
print("A: start");
var instance = Temporal.PlainDate.from({ year: 2000, month: 5, day: 2 });
print("B: from ok " + instance.toString());
var fields = ["years","months","weeks","days","hours","minutes","seconds","milliseconds","microseconds","nanoseconds"];
for (var i = 0; i < fields.length; i++) {
  var f = fields[i];
  print("C: field " + f);
  var bag = {};
  bag[f] = Infinity;
  try {
    instance.add(bag, { overflow: "constrain" });
    print("   no-throw");
  } catch (e) {
    print("   threw " + e.constructor.name + ": " + e.message);
  }
}
print("D: done");
