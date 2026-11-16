
try { var x = String(Symbol("s")); print("String(Symbol) =", x); } catch(e){ print("String(Symbol) throws", e.constructor.name); }
try { var y = String({ toString: function(){ throw new Error("boom"); } }); print("String(obj-throwtoString) =", y); } catch(e){ print("String(obj throw toString) ->", e.constructor.name, e.message); }
try { var z = "" + { toString: function(){ throw new Error("boom2"); } }; print("concat =", z); } catch(e){ print("concat throws ->", e.constructor.name, e.message); }
try { var w = `${ { toString(){ throw new Error("boom3"); } } }`; print("tpl =", w); } catch(e){ print("template ->", e.constructor.name, e.message); }
