
var out = [];
Promise.allSettled([Promise.resolve(1), Promise.resolve(2)]).then(function(v){ print("allSettled ok len=", v.length); });
Promise.all([Promise.resolve(1)]).then(function(v){ print("all ok len=", v.length); });
