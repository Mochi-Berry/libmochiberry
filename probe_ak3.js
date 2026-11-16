
var r1, r2;
var c = Promise.allKeyed({ a: new Promise(function(r){ r1=r; }), b: new Promise(function(r){ r2=r; }) });
c.then(function(res){ print("resolved a=", res.a, "b=", res.b); }, function(e){ print("rej:", e&&e.message); });
r1(1); r2(2);
Promise.resolve().then(function(){ print("flush1 done"); });
Promise.resolve().then(function(){ print("flush2 done"); });
