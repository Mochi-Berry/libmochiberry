// probe class/realm semantics
var g = (typeof $262 !== "undefined" && $262.createRealm) ? $262.createRealm().global : null;
print("createRealm=" + (g ? "yes" : "no"));
print("globalThis=$262? " + (typeof $262));
// class + Reflect.construct + proxy newTarget
class A { constructor(x){ this.x = x; } }
var custom = new Function(); custom.prototype = {};
var o = Reflect.construct(A, [], custom);
print("class custom proto ok=" + (Object.getPrototypeOf(o) === custom.prototype));
var poison = new Proxy(new Function(), { get(t,k){ if(k==='prototype') throw new Error("POISON"); return t[k]; } });
try { Reflect.construct(A, [], poison); print("no throw!"); } catch(e) { print("poison threw: " + e.message); }
try { A(); } catch(e) { print("A() threw " + e.constructor.name); }
class B extends A { constructor(){ super(1); this.c = true; } }
print("extends proto ok=" + (Object.getPrototypeOf(B) === A));
try { B(); } catch(e) { print("B() threw " + e.constructor.name); }
var bi = new B();
print("B inst instanceof A=" + (bi instanceof A));
// new.target
function NT(){ print("new.target=" + (new.target === undefined ? "undef" : "def")); }
try { NT(); } catch(e){}
new NT();
// Symbol.iterator name
var it = { [Symbol.iterator]: function(){ return {next(){return {done:true}}} } };
print("sym iter name=" + it[Symbol.iterator].name);
