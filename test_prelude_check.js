import * as std from "mbjs:std";
let code;
try {
    code = std.loadFile("test262_prelude.js");
} catch (e) {
    console.log("cannot read prelude: " + e.message);
    std.exit(1);
}
try {
    (0, eval)(code);
    console.log("prelude OK: loaded=" + globalThis.__mbjs_prelude.loaded +
        " failed=" + JSON.stringify(globalThis.__mbjs_prelude.failed));
} catch (e) {
    console.log("prelude THREW: " + (e && e.message || e));
    if (e && e.stack) console.log(e.stack.split("\n").slice(0, 6).join("\n"));
    std.exit(1);
}
// sanity: is Intl there?
console.log("Intl=" + (typeof globalThis.Intl) +
    " ListFormat=" + (typeof (globalThis.Intl && globalThis.Intl.ListFormat)) +
    " Temporal=" + (typeof globalThis.Temporal));
