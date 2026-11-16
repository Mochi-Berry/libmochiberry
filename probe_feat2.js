
var ok = {};
try { eval("class A { #x = 1; static #y = 2; get v(){return this.#x;} m(){ return (#x in this); } }"); ok.private_fields = true; } catch(e){ ok.private_fields = "NO:"+e.message; }
try { eval("class B { #x = 1; static m(o){ return o.#x; } }"); ok.private_static_method = true; } catch(e){ ok.private_static_method = "NO:"+e.message; }
ok.weakmap = typeof WeakMap === "function";
ok.proxy = typeof Proxy === "function";
ok.bigint = typeof BigInt === "function";
ok.symbol_asyncIterator = typeof Symbol.asyncIterator === "symbol";
ok.arrayflat = typeof [].flat === "function";
ok.objfromentries = typeof Object.fromEntries === "function";
ok.globalthis = typeof globalThis === "object";
try { eval("let o = {}; let {a = 1, ...r} = o;"); ok.rest_props = true; } catch(e){ ok.rest_props = "NO:"+e.message; }
try { eval("var m = new Map([[1,2]]);"); ok.map = true; } catch(e){ ok.map = "NO"; }
try { eval("class C extends null {}"); } catch(e) {}
try { eval("function f(a=1){}; f.length"); ok.defparam = true; } catch(e){}
try { ok.numeric_sep = (function(){ return eval("1_000"); })() === 1000; } catch(e){ ok.numeric_sep = "NO"; }
try { ok.bigint_tostring = (10n).toString() === "10"; } catch(e){ ok.bigint_tostring = "NO"; }
try { eval("for (const x of []);"); ok.forof = true; } catch(e){}
print(JSON.stringify(ok, null, 1));
