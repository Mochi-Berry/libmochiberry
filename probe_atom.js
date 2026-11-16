
print("Atomics:", typeof Atomics);
print("SharedArrayBuffer:", typeof SharedArrayBuffer);
if (typeof Atomics !== "undefined") {
  print("Atomics.waitAsync:", typeof Atomics.waitAsync);
  print("Atomics.wait:", typeof Atomics.wait);
}
