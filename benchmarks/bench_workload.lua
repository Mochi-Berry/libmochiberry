-- bench_workload.lua -- port of MochiBerry's bench_workload.js
-- Same algorithm, same iteration counts, so the two engines do equal work.
local function fib(n)
  if n < 2 then return n end
  return fib(n - 1) + fib(n - 2)
end

local function sumTo(n)
  local s = 0
  for i = 1, n do s = s + i end
  return s
end

local t = 0
for k = 1, 20 do
  t = t + fib(26) + sumTo(100000)
end

print("BENCH_RESULT " .. t)
