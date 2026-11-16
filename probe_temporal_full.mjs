// Probe: force the FULL Temporal polyfill to install and check the
// Infinity / overflow behaviour of add() (test262 expects RangeError).
print("before import: Temporal=" + typeof Temporal);
delete globalThis.Temporal;
print("after delete:  Temporal=" + typeof Temporal);

globalThis.__mbjs_patches = globalThis.__mbjs_patches || {};
globalThis.js_registry = globalThis.__mbjs_patches;
globalThis.js_record = (n, h) => { globalThis.__mbjs_patches[n] = h; };

await import("./patches/patch_temporal_full.js");

print("after import:  Temporal=" + typeof Temporal);
print("registry:      " + (globalThis.__mbjs_patches.Temporal || "n/a"));

const d = Temporal.PlainDate.from({ year: 2000, month: 5, day: 2 });
print("from:          " + d.toString());

const cases = [
    ["add(+Inf months)", () => d.add({ months: Infinity })],
    ["add(-Inf months)", () => d.add({ months: -Infinity })],
    ["add(+Inf years)",  () => d.add({ years: Infinity })],
    ["add(+Inf days)",   () => d.add({ days: Infinity })],
    ["add(NaN months)",  () => d.add({ months: NaN })],
    ["add(9 months)",    () => d.add({ months: 9 })],
    ["add(120 months)",  () => d.add({ months: 120 })],
];
for (const [label, fn] of cases) {
    try {
        const r = fn();
        print(label + ": returned " + String(r));
    } catch (e) {
        print(label + ": " + e.name + ": " + e.message);
    }
}
