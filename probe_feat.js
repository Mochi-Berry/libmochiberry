print("new.target: " + (function(){ try { return String(new.target); } catch(e){ return "ERR:"+e.message; } })());
function F() { print("in F new.target=" + (new.target === undefined ? "undefined" : String(new.target))); }
try { F(); } catch(e) { print("err " + e.message); }
new F();
var custom = new Function(); custom.prototype = {};
var o = Reflect.construct(F, [], custom);
print("reflect ok proto=" + (Object.getPrototypeOf(o) === custom.prototype));
print("WeakSet=" + typeof WeakSet);
print("Proxy=" + typeof Proxy);
print("Reflect.construct=" + typeof Reflect.construct);
print("Symbol.toStringTag=" + typeof Symbol.toStringTag);
print("getOwnPropertyNames order test:");
var ob = {}; Object.defineProperty(ob, "segment", {value:1,enumerable:true,writable:true,configurable:true});
Object.defineProperty(ob, "index", {value:2,enumerable:true,writable:true,configurable:true});
Object.defineProperty(ob, "input", {value:3,enumerable:true,writable:true,configurable:true});
print(JSON.stringify(Object.getOwnPropertyNames(ob)));
print("String.fromCodePoint=" + typeof String.fromCodePoint);
print("codePointAt=" + typeof "".codePointAt);
print("Number.isFinite=" + typeof Number.isFinite);
print("Object.setPrototypeOf=" + typeof Object.setPrototypeOf);
print("getter on proto: " + (typeof Object.getOwnPropertyDescriptor(Object.prototype, "x")));
