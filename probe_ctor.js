
function isCtor(f){ try { Reflect.construct(f, []); return "ctor"; } catch(e){ return "not-ctor:"+e.constructor.name; } }
// 1. plain arrow assigned to var
var f1 = (obj) => { return this === undefined ? "undef" : (typeof this === "object" ? "obj" : typeof this); };
// 2. proxy over arrow with apply trap forwarding thisArg
var t2 = (obj) => { return this; };
var f2 = new Proxy(t2, {
  apply: function(tt, th, args){ return tt.apply(th, args); },
  construct: function(){ throw new TypeError("not a ctor"); }
});
// 3. proxy over function
var t3 = function(obj){ return this; };
var f3 = new Proxy(t3, { construct: function(){ throw new TypeError("nope"); } });
var out = {};
out.f1_name = f1.name; out.f1_len = f1.length; out.f1_ctor = isCtor(f1);
out.f2_name = f2.name; out.f2_len = f2.length; out.f2_ctor = isCtor(f2);
out.f2_this = f2.call({x:1}, 5) === undefined ? "undef" : "forwarded";
out.f3_name = f3.name; out.f3_len = f3.length; out.f3_ctor = isCtor(f3);
out.f3_this = f3.call({x:1}, 5) === undefined ? "undef" : "forwarded";
print(JSON.stringify(out));
