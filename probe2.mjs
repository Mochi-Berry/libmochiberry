var o = {};
o.foo = function() {};
print("assign-name:", JSON.stringify(o.foo.name), "len", o.foo.length);
function C(){}
C.prototype.bar = function(a,b){};
print("proto-assign-name:", JSON.stringify(C.prototype.bar.name), "len", C.prototype.bar.length);
print("toStringTag support:", typeof Symbol.toStringTag);
var p = {};
Object.defineProperty(p, Symbol.toStringTag, {value:"X", writable:false, enumerable:false, configurable:true});
print("tostring:", Object.prototype.toString.call(p));
var d = Object.getOwnPropertyDescriptor(C, "prototype");
print("proto desc:", JSON.stringify({w:d.writable,e:d.enumerable,c:d.configurable}));
try { Object.defineProperty(C, "prototype", {writable:false}); print("redefine prototype ok"); } catch(e){ print("redefine err", e.message); }
var e2 = Object.getOwnPropertyDescriptor(C, "prototype");
print("proto desc2:", JSON.stringify({w:e2.writable,e:e2.enumerable,c:e2.configurable}));
print("getOwnPropertyNames order:", JSON.stringify(Object.getOwnPropertyNames({locale:1,type:2,style:3})));
print("Symbol.iterator string iter:", JSON.stringify(Array.from("abc")));
