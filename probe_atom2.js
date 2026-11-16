
print("agent:", (typeof $262!=="undefined" && typeof $262.agent) );
print("notify:", typeof Atomics.notify);
var sab = new SharedArrayBuffer(1024);
var i32 = new Int32Array(sab);
print("isShared i32:", Atomics.isLockFree ? "has-isLockFree" : "-");
// non-shared wait should throw; wait on shared with mismatched value returns "not-equal"
var nons = new Int32Array(4);
try { Atomics.wait(nons, 0, 0, 1); print("wait non-shared: NO THROW"); } catch(e){ print("wait non-shared: "+e.constructor.name); }
print("wait shared not-equal:", Atomics.wait(i32, 0, 12345, 1)); // value mismatch -> not-equal, no block
print("wait shared timeout:", Atomics.wait(i32, 0, 0, 1)); // value==0, times out after 1ms
