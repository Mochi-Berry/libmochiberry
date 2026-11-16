# Pure-JS feature patches (patch_*)

`patches/patch_*.js` are **pure JavaScript** files that bring ECMAScript
2025 / 2026 features to any libmochiberry build that lacks them. They are the
OTA vehicle for language features: because they are plain JS and versioned,
shipping a new feature is shipping one small text file — no engine recompile,
no firmware reflash.

## Conventions

- **Pure JS only.** No `MBJS_*` C API, no engine internals. Each file is a
  plain script that installs its features with `Object.defineProperty`.
- **`js_`-prefixed helpers.** Internal helpers use the pure-JS `js_` prefix
  ("embel-embel JS_/js* murni") so they never collide with the `MBJS_*`
  engine API or application code.
- **Self-skipping.** Every feature is guarded: if the engine already provides
  it natively, the patch leaves it untouched and records `"native"`. Patches
  are therefore safe to run on any engine version, old or new.
- **Self-reporting.** Everything is recorded in
  `globalThis.__mbjs_patches` (feature name -> how it was resolved) for
  `patch_loader.js` to report.
- **`"use strict"` and no top-level side effects** beyond the guarded
  installs.

## Running

Apply everything and print the report:

```sh
build/mbjs -m patches/patch_loader.js
```

Regression test:

```sh
build/mbjs -m tests/test_patches.js     # or: make patchtest
```

## Current coverage

| File                     | Features                                               |
| ------------------------ | ------------------------------------------------------ |
| `patch_ecma2025.js`      | `Promise.try`, `RegExp.escape`, Set methods, `Math.f16round`, `JSON.parse` source-text access |
| `patch_ecma2026.js`      | `String.dedent`, `Error.isError`, `Intl.DurationFormat` (minimal shim), notes on syntax-only proposals (`import defer`, `import attributes`) |
| `patch_v8compat.js`      | V8/Node runtime surface: `structuredClone`, `TextEncoder`/`TextDecoder`, timers (`setTimeout`/`setInterval`, backed by `mbjs:os`), `URL`/`URLSearchParams`, `crypto.getRandomValues` (non-secure shim), `fetch` stub |

Syntax-level proposals cannot be polyfilled in JS; those are delivered by the
engine itself. The loader's report marks them explicitly so the gap is
visible.

## V8 compatibility

V8's *syntax* is standard ECMA-262, which the engine implements (plus the
ECMA2025/2026 patches). `patch_v8compat.js` closes the *runtime* gap so code
written for V8/Node runs unmodified:

- `structuredClone` — deep clone with cycle detection.
- `TextEncoder` / `TextDecoder` — full UTF-8 with overlong-sequence
  rejection (U+FFFD per the encoding standard).
- Timers — `setTimeout`/`setInterval`/`clear*`, backed by the engine's
  `mbjs:os` module (the loader exposes it to patches as
  `globalThis.__mbjs_os`).
- `URL` / `URLSearchParams` — full parse/stringify for http(s) URLs.
- `crypto.getRandomValues` / `randomUUID` — a **non-secure** shim; embedders
  with a hardware RNG should replace it.
- `fetch` — a rejecting stub (the engine core has no network layer);
  embedders install a real one.

Patches receive the engine's system modules through the loader's patch ABI
(`globalThis.__mbjs_os`, `globalThis.__mbjs_std`); they must only read them.

## Adding a patch

1. Name it `patches/patch_<feature>.js` (the loader discovers it automatically).
2. Guard with `if (!js_has(<target>, "<name>"))`, install with
   `Object.defineProperty`, and record with `js_record(...)`.
3. Add a `check(...)` in `tests/test_patches.js` and run
   `make patchtest`.

## OTA

`scripts/make-ota.sh` copies every `patches/patch_*.js` into the OTA bundle
(see `ota/`). A device applies them at boot through the same loader, so an
OTA can deliver: a new app (bytecode), a new engine (`.so`), and new language
features (patches) — each independently versioned and checksummed in
`manifest.json`.
