# OTA upgrades for libmochiberry

The whole point of the OTA layout is: **an upgrade must be a file swap, never
a re-code.** The repo is structured so the engine, the app and the language
features can each move forward independently over the air.

## Why this is OTA-friendly

| Layer              | Artifact                         | Upgraded by                     |
| ------------------ | -------------------------------- | ------------------------------- |
| Engine             | `libmochiberry.so`               | swapping the `.so` (rare)       |
| Application        | `app.mbc` (compiled bytecode)    | swapping the bytecode (common)  |
| Language features  | `patches/patch_*.js` (pure JS)   | dropping in a new patch file    |
| Contract           | `manifest.json` + `SHA256SUMS`   | regenerated every build         |

- **Apps are bytecode, not source.** `mbjsc` compiles `app.js` to `app.mbc`;
  the device evaluates bytecode with `mbjs_std_eval_binary()`. Updating an app
  is shipping a new `.mbc` — no compiler on the device, no re-flash.
- **Stable ABI.** The `MBJS_*` C API and the bytecode format are tied to the
  engine version recorded in the manifest's `abi` field; the device refuses
  bundles whose ABI does not match its loader.
- **Features arrive as pure-JS patches.** New (or proposal-stage) ECMAScript
  features live in `patches/patch_*.js`, written in plain JS with `js_`
  helpers, applied at boot. Shipping a feature = shipping one small text file.

## Building a bundle

```sh
sh scripts/make-ota.sh [version] [app.js]
```

Produces `dist/ota/<version>/`:

```
dist/ota/0.1.0/
  manifest.json          # machine-readable contract (see manifest.schema.json)
  SHA256SUMS             # checksums for every artifact
  libmochiberry.so       # the engine
  app.mbc                # the application, compiled to bytecode
  app.js                 # the application source (for audit)
  patches/patch_ecma2025.js
  patches/patch_ecma2026.js
```

and a single-file bundle `dist/ota/libmochiberry-<version>-ota.zip`.

## Device-side flow (recommended)

1. Download the bundle zip (or a directory of files) over your transport of
   choice — HTTP, MQTT, BLE, raw partition write.
2. Verify every file against `SHA256SUMS` (and the entries embedded in
   `manifest.json`).
3. Check `manifest.abi` against the loader's ABI tag. On mismatch, refuse and
   keep the previous bundle (`upgrade.rollback`).
4. Stage the new files, then atomically swap: engine `.so` → restart the
   engine; app `.mbc` → next app start; patches → applied at next boot by
   `patch_loader.js` (see `patches/`).
5. On boot failure or watchdog reset, fall back to `upgrade.rollback`.

Nothing in that flow compiles anything, which is exactly the property the
repo is engineered for: the sandbox builds `dist/` from the same sources the
CI builds, so a shipped bundle is guaranteed to match the code in the tree.
