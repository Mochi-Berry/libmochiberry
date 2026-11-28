# libmochiberry (mochi_modules) — source layer marker

MochiBerry engine — embeddable C runtime with the JS engine, module loader and builtin library

This repository is an **upstream source tree**, not an installable shim
module, so this directory contains no prebuilt binary and no plugin entry
point. It records the native build instead:

```text
mochi_modules/
├── manifest.json   build command + artifact map (no fake binary)
└── README.md       this note
```

## Build

```bash
cmake -S . -B build && cmake --build build
```

Produces:

* build/mbjs (the engine executable)
* build/libmochiberry.* (embeddable library)

## Consumers

the per-module repos under Mochi-Berry build against this engine's headers (mbjs.h, mbjs_runtime.h)

## Note

This repository is the engine source. It ships no prebuilt binary and no JavaScript package; the two layer directories exist so every repo in the org has the same shape and the loader can resolve them uniformly.
