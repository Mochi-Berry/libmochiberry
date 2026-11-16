// bench_workload.js — identical workload to benchmarks/bench_tiers.c WORK_BIG.
// Kept as a file so mbjsc -e can embed it into a standalone AOT app.
(function(){
  function fib(n){ return n < 2 ? n : fib(n-1) + fib(n-2); }
  function sumTo(n){ var s=0; for (var i=1;i<=n;i++) s+=i; return s; }
  var t = 0;
  for (var k=0;k<20;k++) t += fib(26) + sumTo(100000);
  console.log("BENCH_RESULT " + t);
  return t;
})();
