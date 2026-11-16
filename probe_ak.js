
var __p = globalThis.__mbjs_prelude;
print("prelude loaded:", __p.loaded, "failed:", JSON.stringify(__p.failed));
print("allKeyed:", typeof Promise.allKeyed, "allSettledKeyed:", typeof Promise.allSettledKeyed);
var d = Object.getOwnPropertyDescriptor(Promise.allKeyed, "name");
print("name desc:", JSON.stringify(d));
var d2 = Object.getOwnPropertyDescriptor(Promise.allKeyed, "length");
print("length desc:", JSON.stringify(d2));
// minimal already-resolved case
var combined = Promise.allKeyed({ a: Promise.resolve(1), b: Promise.resolve(2) });
var state = "pending";
combined.then(function(r){
  state = "resolved";
  print("RESOLVED proto-null:", Object.getPrototypeOf(r) === null, "keys:", Object.keys(r).join(","), "a:", r.a, "b:", r.b);
}, function(e){ state="rejected"; print("REJECTED:", e && e.message); });
// force a job flush
Promise.resolve().then(function(){ print("state after flush:", state); });
