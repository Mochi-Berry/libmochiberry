import "./test262_prelude_intl.js";
print("prelude=" + JSON.stringify(globalThis.__mbjs_prelude));
var lf = new Intl.ListFormat("en", {type:"unit"});
print(lf.format(["a","b"]));
print(JSON.stringify(lf.resolvedOptions()));
print(Object.prototype.toString.call(lf));
print(lf.format(["a","b","c"]));
print(JSON.stringify(new Intl.ListFormat("en").formatToParts(["a","b","c"])));
