# MBJS for Berry OS (libmochiberry)

This directory is the embedded-device port of the **libmochiberry** JavaScript
engine. Berry OS class devices (ESP32, STM32, nRF52, ...) get a real, modern
ECMAScript engine instead of a scripting-language subset, and they get it with
a memory story that fits a few hundred kilobytes of heap:

- **No GC for the C side.** `mbjs-mem` provides a deterministic bump arena.
  The engine's C allocations all come from it and are reclaimed in bulk with
  one reset — no garbage collector, no reference counting, no stop-the-world
  pauses. JS-level objects still use the engine's reference counting +
  cycle collector, exactly like upstream.
- **Ownership-marked handles.** Native modules mark every pointer they pass
  around as *borrowed* or *taken* (`mbjs_mem_borrow()` / `mbjs_mem_take()`).
  `mbjs_mem_arena_audit()` turns "forgot to free" into a boot-time test
  failure instead of a slow leak.
- **Hard budgets.** The arena has a byte limit and the VM has a JS stack
  limit, so a runaway app fails cleanly instead of starving the device.
- **OTA-ready.** The engine ships as `libmochiberry.so`, apps ship as
  bytecode, and an OTA manifest describes the pairing — see `ota/`. Devices
  can swap apps (or the engine) over the air without re-coding anything.

## Building for a device

Two options, both producing the same `MBJS_*` API:

### 1. Amalgam (recommended for toolchains without CMake)

```sh
# on the host, produce the amalgamated source:
build/mbjs amalgam.js dist/amalgam/mbjs-amalgam.c
```

Then cross-compile for your target, e.g. with the ESP-IDF Xtensa toolchain:

```sh
xtensa-esp32s3-elf-gcc -O2 -fPIC -DMBJS_BUILD_LIBC \
    -I. -Iplatform/berry \
    -c dist/amalgam/mbjs-amalgam.c -o libmochiberry.o
xtensa-esp32s3-elf-ar rcs libmochiberry.a libmochiberry.o
```

Link `libmochiberry.a`, `mbjs-mem.c`, `platform/berry/mbjs_berry.c` and
`mbjs.h`/`mbjs-libc.h`/`mbjs-mem.h` into your firmware.

### 2. CMake

The main build can cross-compile too; see `docs/docs/building.md`. The
`mbjs_berry` adapter needs no special flags.

## Using the adapter

```c
#include "platform/berry/mbjs_berry.h"

static MBJSBerryEngine g_engine;

void app_start(void) {
    /* 512 KiB arena budget, 256 KiB JS stack. */
    if (mbjs_berry_init(&g_engine, 0, 0) != 0)
        fatal("engine boot failed");

    mbjs_berry_eval(&g_engine,
                    "console.log('berry up:', 1 + 1);",
                    34, "<boot>");
}

void ota_apply_bytecode(const uint8_t *bc, size_t len) {
    /* apps arrive as bytecode; see ota/ for the manifest flow */
    mbjs_std_eval_binary(g_engine.ctx, bc, len, 0);
}

void app_stop(void) {
    mbjs_berry_destroy(&g_engine); /* one reset reclaims everything */
}
```

## Memory guidance

| Budget knob            | Default       | Meaning                                   |
| ---------------------- | ------------- | ----------------------------------------- |
| `MBJS_BERRY_HEAP_LIMIT`| 512 KiB       | arena budget for all engine C allocations |
| `MBJS_BERRY_STACK_LIMIT`| 256 KiB      | JS stack budget                           |
| `MBJS_BERRY_GC_THRESHOLD`| 256 KiB     | JS-object bytes before the cycle collector runs |
| `MBJS_BERRY_ARENA_BLOCK`| 16 KiB       | arena block size                          |

Tune `berry_config.h` (or `-D`) to the free heap of your device. The arena
grows in `MBJS_BERRY_ARENA_BLOCK` steps, so a smaller block size wastes less
on partially used blocks.

## OTA upgrades

The OTA contract lives in `ota/`. In short:

1. `sh scripts/make-ota.sh` builds `libmochiberry.so`, compiles the app to
   bytecode, and writes a versioned bundle + manifest to `dist/ota/`.
2. The device downloads the bundle, verifies the SHA-256 entries in the
   manifest, and swaps the app bytecode (or the engine .so).
3. Patches under `patches/` (pure-JS, versioned) are applied at boot, so new
   ECMAScript features can be delivered without recompiling the engine.

Because apps are bytecode and the engine exposes a stable `MBJS_*` ABI, an
upgrade is a file swap — no re-coding, no re-flashing of the firmware that
embeds the loader.
