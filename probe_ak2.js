
print("native Promise.resolve:", typeof Promise.resolve);
var p0 = Promise.resolve(1);
print("p0 thenable typeof:", typeof p0.then);
p0.then(function(v){ print("p0 resolved v=", v); }, function(e){ print("p0 rejected", e&&e.message); });
// 2. does a minimal keyed run resolve? use default C=Promise
var r2 = Promise.allKeyed({ a: Promise.resolve(5) });
var n = 0;
r2.then(function(res){
  n++;
  print("ak resolved n=", n, "proto-null:", Object.getPrototypeOf(res)===null, "a:", res.a);
}, function(e){ n++; print("ak rejected n=", n, e&&e.message); });
// 3. simulate exactly what the test's key-order does (mixed settle order, 3 keys)
var rf, rs, rt;
var input = { first: new Promise(function(res){ rf=res; }), second: new Promise(function(res){ rs=res; }), third: new Promise(function(res){ rt=res; }) };
var combined = Promise.allKeyed(input);
combined.then(function(res){
  print("key-order resolved keys:", Object.keys(res).join(","));
}, function(e){ print("key-order rejected:", e&&e.message); });
rs("s"); rt("t"); rf("f");
Promise.resolve().then(function(){ print("after jobs: n=", n); });
