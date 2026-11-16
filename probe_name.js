
var a = (x) => {};
print("arrow name desc:", JSON.stringify(Object.getOwnPropertyDescriptor(a, "name")));
try { Object.defineProperty(a, "name", {configurable: true}); print("redefine arrow name: OK"); }
catch(e){ print("redefine arrow name THROWS:", e.constructor.name, e.message); }
var proxy = new Proxy(a, {});
try { Object.defineProperty(proxy, "name", {configurable: true}); print("redefine proxy name: OK"); }
catch(e){ print("redefine proxy name THROWS:", e.constructor.name, e.message); }
