// Probe: force the minimal Temporal polyfill to install even where the
// engine ships a native Temporal, then exercise the add() Infinity path.
print("before import: Temporal=" + typeof Temporal);
delete globalThis.Temporal;
print("after delete:  Temporal=" + typeof Temporal);

globalThis.__mbjs_patches = globalThis.__mbjs_patches || {};
globalThis.js_registry = globalThis.__mbjs_patches;
globalThis.js_record = (n, h) => { globalThis.__mbjs_patches[n] = h; };

await import("./patches/patch_temporal_min.js");

print("after import:  Temporal=" + typeof Temporal);
print("registry:      " + (globalThis.__mbjs_patches.Temporal || "n/a"));

const d = Temporal.PlainDate.from({ year: 2000, month: 5, day: 2 });
print("from:          " + d.toString());

// 1) Infinity must throw RangeError, never hang (while (m > 12) guard).
try {
    d.add({ months: Infinity });
    print("add(+Inf months): NO THROW  <-- BUG");
} catch (e) {
    print("add(+Inf months): " + e.name + ": " + e.message);
}

// 2) -Infinity must throw RangeError too (while (m < 1) guard).
try {
    d.add({ months: -Infinity });
    print("add(-Inf months): NO THROW  <-- BUG");
} catch (e) {
    print("add(-Inf months): " + e.name + ": " + e.message);
}

// 3) Normal month roll-over stays correct.
print("add(9 months):    " + d.add({ months: 9 }).toString());
print("subtract(6 mo):   " + d.subtract({ months: 6 }).toString());

// 4) Non-object duration argument must not crash the shim.
try {
    print("add(object):      " + d.add(Object.create(null)).toString());
} catch (e) {
    print("add(object):      " + e.name + ": " + e.message);
}
