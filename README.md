# ⚡️ MBJS (libmochiberry) — a mighty, embeddable JavaScript engine

MBJS is a small, fast and embeddable JavaScript engine aiming for the latest
[ECMAScript] specification. This repository, **libmochiberry** (package
`my.berrylabfoundation.mochiberry`), is a fork of QuickJS (by Fabrice Bellard
and Charlie Gordon) that was fully rebranded to the MBJS namespace and
hardened for embedded / OTA-updatable devices — with AOT, JIT and native
binary tooling on top.

```
qjs/qjsc  →  mbjs/mbjsc        (binaries)
JS_/js_   →  MBJS_/mbjs_       (C API + internal helpers)
QJS_       →  MBJS_            (build options, env vars)
quickjs    →  mbjs             (files, docs, module specifiers)
```

## What this repo gives you

| Piece | Where | What it is |
| ----- | ----- | ---------- |
| The engine | `mbjs.c`, `mbjs.h` | ECMAScript VM, compiler, std/os/bjson modules (`mbjs-libc.c`) |
| CLI + compiler | `mbjs`, `mbjsc` | interpreter/REPL and bytecode compiler |
| **Purity gate** | `scripts/purify.sh` | audits the tree for legacy QuickJS tokens; currently **100.00% pure** |
| **Memory foundation** | `mbjs-mem.h/c` | non-GC arena allocator + ownership-marked handles (borrowed/taken), leak audit, **bare-metal region mode** |
| **JIT foundation** | `mbjs-jit.h/c` | pluggable just-in-time tier: runtime C compilation (`cc` backend) + hot-function tracker; degrades to interpreter on bare metal |
| **AOT native builder** | `scripts/build-native.sh` | compile a JS app to a native executable (Bun/Clang style), static or shared |
| **Cross-compile matrix** | `scripts/cross-build.sh` | `libmochiberry.{a,o,so,dll}` for arm32, arm64, riscv32, riscv64, x86_32, x86_64, MinGW |
| **Berry OS port** | `platform/berry/` | embed adapter with hard memory budgets for ESP32-class devices |
| **Bare-metal / custom OS port** | `platform/baremetal/` | boot the engine inside one RAM region, no malloc, no OS (`make baremetal-check` proves zero libc refs) |
| **OTA packaging** | `ota/`, `scripts/make-ota.sh` | versioned bundles: engine `.so` + app bytecode + patches + manifest |
| **ES2025/2026 + V8 patches** | `patches/patch_*.js` | pure-JS feature patches applied at boot, no engine recompile |
| **Shared library** | `scripts/build-libmochiberry.sh` | builds `dist/libmochiberry.{so,a,o,pc}` + a minimal C host |

## Package identity (reverse domain)

The packaged artifact is always `libmochiberry` (`.so` / `.dll` / `.a` / `.o`)
with the reverse-domain package name:

```c
#define MBJS_PACKAGE_NAME    "my.berrylabfoundation.mochiberry"
#define MBJS_PACKAGE_ORG     "my.berrylabfoundation"
#define MBJS_PACKAGE_ARTIFACT "mochiberry"
#define MBJS_PACKAGE_LIBRARY  "libmochiberry"
#define MBJS_ECMA_LEVEL       2026
```

`MBJS_GetPackageName()` returns it at runtime; package.json carries
`"packageName"`, the OTA manifest carries it in the `package` field, and
`libmochiberry.pc` exposes it to pkg-config.

## Building

Requires a C toolchain and CMake. No third-party dependencies, no env vars.

```sh
make           # builds build/mbjs, build/mbjsc, test tools
make memtest   # memory-management tests (arena + ownership handles)
make jittest   # JIT tests (runtime C compilation, hot tracking)
make patchtest # ES2025/2026 + V8-compat patch regression tests
make purify    # purity gate: fails if any legacy QuickJS token leaks in
make native    # AOT-compile the demo app to a native executable
make cross     # cross-compile matrix report (see below)
make baremetal-check  # verify the memory foundation has zero libc refs
```

Run a script:

```sh
build/mbjs examples/app_mochiberry.js
```

## AOT — compile a JS app to a native binary (Bun/Clang style)

`scripts/build-native.sh <app.js> [out] [--static|--shared|--fully-static]`
turns an app into a standalone native executable: JS → bytecode (mbjsc) →
C (`-e` codegen) → machine code (`cc -O2`):

```sh
make native                          # dist/mochiberry-native (static)
dist/mochiberry-native               # runs the app, no interpreter involved

sh scripts/build-native.sh my-app.js my-app --static --strip
file my-app                          # ELF 64-bit x86-64 ...
```

- `--static` (default) links `libmochiberry.a`: a single self-contained
  binary, no `.so` to ship.
- `--shared` links `libmochiberry.so` for a smaller binary.
- `--fully-static` additionally static-links libc.
- `--lto` enables link-time optimization; `--strip` shrinks the binary.
- `--o3` compiles at `-O3` instead of `-O2`; `--native` adds
  `-march=native -mtune=native` (fastest on this machine, less portable).

```sh
sh scripts/build-native.sh my-app.js my-app --o3 --native --lto --strip
```

The AOT binary embeds the app's bytecode and the engine, so it is a real
standalone executable — the same JS runs identically in the interpreter
and in the compiled binary (`make bench` proves the results agree).

## JIT — native speed when you want it

`mbjs-jit` adds a **pluggable JIT tier** on top of the interpreter without
touching the engine core:

- `mbjs_jit_compile()` compiles a JS function to **native machine code at
  runtime**: the source is wrapped in a tiny C shim, compiled with `cc -O2`
  and loaded with `dlopen`. Compile-on-first-use, exactly like a classic JIT
  pipeline.
- `mbjs_jit_observe()` is the hot-function tracker: count calls, and once a
  function crosses the threshold it is compiled once and its native pointer
  is handed back for direct calls. Stats report how many calls actually ran
  natively.
- `mbjs_jit_call()` compiles-or-falls-back to the interpreter, so the same
  code runs everywhere. The compiled function is **cached by (source,
  name)**: the first call pays the `cc` + `dlopen` cost, every later call
  with the same source dispatches straight to the cached native pointer
  (bounded cache — `MBJS_JIT_CALL_CACHE_MAX` entries, oldest dropped, so
  hot loops stay native without unbounded growth). `stats.cache_hits`
  counts the fast-path dispatches.
- **Persistent compilation cache (`.mbjso`)** — pass `cache_dir` in
  `MBJSJitOptions` and every freshly compiled shim is atomically stored as
  `cache_dir/<key>.so`. The key is a content hash of
  (source, engine version, cc flags, pointer size), so a rebuilt engine or
  a different toolchain self-invalidates stale entries. On the next boot,
  the same `cache_dir` dlopens the cached shims and **skips `cc` entirely**
  (`stats.cache_disk_hits` / `cache_disk_writes` count the traffic) — the
  JIT pays the toolchain once per device, not once per start. The shim is
  self-contained: it embeds the JS source, so a cached `.so` can even be
  shipped as a prebuilt artifact to devices that have no compiler.
- On bare-metal / custom OSes (no compiler, no dlopen) the backend resolves
  to `"none"` and everything degrades to the interpreter — the API does not
  change.

```sh
make jittest   # compiles fib, calls it natively, checks results match
make bench     # interpreter vs JIT vs AOT-native on the same workload
```

`make bench` runs one identical workload (`examples/bench_fib.js`) through
all three engines and reports wall times plus a speedup column. The
interpreter and the AOT binary produce byte-identical output; the JIT
numbers show the compile-once + cached-dispatch tier on top of the
interpreter (`compile_ok=2 cache_hits=38 native_calls=40` for the stock
workload — the first two calls compile, everything after is a cache hit).
The bench then boots a **second JIT against the same `cache_dir`**: the
`JIT2` line reports `cache_disk_hits=2 compile_attempts=0`, i.e. the shims
from the first boot are dlopen'd and the C compiler is never invoked
again (~50 ms vs ~1000 ms for the cold boot).

```c
MBJSJit *jit = mbjs_jit_new(MBJS_JIT_BACKEND_AUTO, NULL);
MBJSJitFn fn = mbjs_jit_compile(jit, ctx, "(function (n) { return n * 2; })", "dbl");
MBJSValue r = fn(ctx, MBJS_UNDEFINED, 1, argv);   /* native call */
```

## Cross-compiling — all 7 architecture families

`sh scripts/cross-build.sh` builds `libmochiberry.{a,o}` for all **seven**
requested architecture families (plus `.so` on Linux targets and `.dll` on
MinGW), detecting each toolchain by prefix and reporting install hints for
anything missing. The ISA facts behind each target (registers, modes,
calling conventions) are researched and documented in
`docs/ARCHITECTURES.md`:

| arch | triplet | artifact |
| ---- | ------- | -------- |
| x86_16 (16-bit DOS/bare-metal) | `x86_16` — `ia16-elf-gcc`, else `cc -m16` | `.a` `.o` + flat `.bin` |
| x86_32 i386 | `i386-linux-gnu` (`-m32 -march=i386`) | `.a` `.o` `.so` |
| x86_32 i686 | `i686-linux-gnu` | `.a` `.o` `.so` |
| x86_64 / amd64 | host (`dist/native`) | `.a` `.o` `.so` |
| arm32 | `arm-linux-gnueabihf` | `.a` `.o` `.so` |
| arm64 | `aarch64-linux-gnu` | `.a` `.o` `.so` |
| riscv32 (bare-metal) | `riscv32-unknown-elf` | `.a` `.o` |
| riscv64 | `riscv64-linux-gnu` | `.a` `.o` `.so` |
| win64 (bonus) | `x86_64-w64-mingw32` | `.dll` `.dll.a` `.a` `.o` |

x86_16 specifics: a real 16-bit toolchain (`ia16-elf-gcc`) produces genuine
8086/286 segmented code; otherwise the host compiler's `-m16` mode is used
(32-bit prefixed code that runs in real mode on 386+ CPUs — see
`platform/baremetal/link16.ld`). `make x86_16-check` verifies the memory
foundation compiles clean in `-m16` with zero libc references (it SKIPs
gracefully when the host lacks 32-bit multilib).

```sh
make cross            # native (x86_64) builds; others report install hints
make x86_16-check     # 16-bit freestanding compile + no-libc check
CROSS_PREFIX=aarch64-linux-gnu- sh scripts/cross-build.sh   # one target
```

Outputs land in `dist/<triplet>/` with headers in `dist/<triplet>/include/`.
For MCU builds, compile the bare-metal adapter freestanding as shown in
`platform/baremetal/README.md`.

## Transpilers — JS → C / C3 / GAS / LLVM / beasm

`transpilers/mbjs-to-native.js` is a self-hosted JS-to-native transpiler
(parses a subset of JS and emits five freestanding targets, no libc
required):

```sh
build/mbjs -m transpilers/mbjs-to-native.js -- app.js --c    -o app.c     # C (gcc/clang)
build/mbjs -m transpilers/mbjs-to-native.js -- app.js --c3   -o app.c3    # C3 (asm{})
build/mbjs -m transpilers/mbjs-to-native.js -- app.js --gas  -o app.s     # GNU as
build/mbjs -m transpilers/mbjs-to-native.js -- app.js --llvm -o app.ll    # LLVM IR
build/mbjs -m transpilers/mbjs-to-native.js -- app.js --basm -o app.basm  # Berry assembler
```

- **C / C3** — register-based IR, stack frames, forward declarations;
  C3 output uses `asm { ... }` blocks for the freestanding bits.
- **GAS** — x86-64 AT&T with a proper `subq` frame so push/pop never
  clobber locals; assembles with plain `as --64` and links with `ld` (the
  generated code talks to the OS via raw `syscall`, no libc).
- **LLVM** — typed SSA IR with `br`/`icmp`/`add`/`ret`, runnable through
  `lli` or `llc`.
- **beasm** — Berry OS assembly (Intel syntax) with a shift-subtraction
  `__mbjs_divmod` helper (beasm has no `idiv`) implementing JS signed
  division/remainder semantics (truncate toward zero, remainder takes the
  dividend's sign). Assembles with `beasm` into a `BEXI64` Berry binary.

```sh
make transpile   # JS -> freestanding C, built and run natively
make basm        # JS -> Berry assembler (.basm), assembled via beasm

# verify a Berry binary on a normal host:
sh scripts/basm-to-gas.sh app.basm app.s --run   # converts to GAS, runs it
```

`scripts/basm-to-gas.sh` converts beasm output to GNU as AT&T and remaps
the Berry syscalls (SOW → write, WITHER → exit) to Linux so a Berry OS
binary can be executed and verified on a plain dev machine. The stock demo
transpiles through all five targets and prints identical results
(`sum 1..10 = 55`, `fib(20) = 6765`, ...) on every backend.

## How pure is MBJS? (purifikasi)

`scripts/purify.sh` scans every tracked source, doc and build file for legacy
QuickJS tokens (`qjs`, `qjsc`, `QJS`, `JS_*`, `js_*`, `JS<Upper>`,
`quickjs`, `JS_NAN_BOXING`, ...) — JSON and `mbjs*` identifiers are
deliberately not impurities, and upstream URLs are exempt.

```
$ sh scripts/purify.sh --check
  files scanned : 188
  lines scanned : 128138
  legacy lines  : 0
  purity score  : 100.00%
purify: PASSED - tree is 100% MBJS-pure.
```

The check runs in CI (`purity` job) and as `make purify`. If a merge ever
re-introduces legacy names, `sh scripts/purify.sh --fix` applies the
deterministic rename rules, or `scripts/brand-mbjs.sh` re-runs the full
rebrand.

## Memory management — no GC, explicit ownership only

`mbjs-mem` is the fork's own memory-management foundation. It is deliberately
**not** a garbage collector:

- **Bump arena.** Allocations are carved from blocks; individual frees are
  no-ops; `mbjs_mem_arena_reset()`/`destroy()` reclaim everything in bulk.
- **Ownership-marked handles.** Every pointer can be tagged
  `MBJS_MEM_TAKEN` (owner must release), `MBJS_MEM_BORROWED` (may read, never
  free) or `MBJS_MEM_FREE`. Ownership moves only through explicit
  `mbjs_mem_take()` / `mbjs_mem_borrow()` / `mbjs_mem_release()` /
  `mbjs_mem_abandon()` — the "parameter diambil" marker for APIs that transfer
  ownership to the callee.
- **Leak audit.** `mbjs_mem_arena_audit()` reports handles still open when
  the arena resets; forgetting to release becomes a test failure, not a leak.
- **Engine integration.** `mbjs_mem_mf()` provides the allocator functions
  for `MBJS_NewRuntime2()`, so the whole runtime can run on the arena with a
  hard byte limit.
- **Bare-metal region mode ("kosongan").** `mbjs_mem_arena_init_region(a,
  region, size)` seeds the arena with a caller-owned fixed memory region —
  no malloc, no free, no libc. Compile with `-DMBJS_MEM_NO_LIBC` and the
  object file contains **zero** libc references (verified by
  `make baremetal-check`).

```c
MBJSMemArena a;
mbjs_mem_arena_init(&a, 0);
mbjs_mem_arena_set_limit(&a, 512 * 1024);          /* hard budget */
MBJSRuntime *rt = MBJS_NewRuntime2(mbjs_mem_mf(), &a);
/* ... run the app ... */
MBJS_FreeRuntime(rt);
mbjs_mem_arena_destroy(&a);                        /* one bulk reclaim */
```

See `mbjs-mem.h` and `tests/test_mem.c`.

### Sunat GC — the circumcised runtime (`make sunat`)

`make sunat` builds **`dist/sunat/libmochiberry.so`** — a runtime variant in
which the garbage collector is circumcised out and only the memory
management remains:

- `mbjs_trigger_gc()` and `MBJS_RunGC()` are **no-ops**: the mark & sweep
  never runs, there are no GC pauses, and `gc_runs` in the stats is always 0.
- Every byte the engine allocates lives in **one deterministic mbjs-mem
  arena** (bump allocator, no per-object free). Memory usage is a monotone
  bump (pooled internally by the engine's small-block arena, so long-lived
  steady-state workloads stay bounded) that is **fully reclaimed in bulk at
  `MBJS_FreeRuntime()`**.
- Cycles are fine: `a.self = a` just works — nothing collects them until
  teardown, then everything goes at once.

Two ownership paths, same guarantee:

```c
/* 1. runtime-owned arena (default): MBJS_NewRuntime() creates the arena and
      MBJS_FreeRuntime() destroys it — no extra code. */
MBJSRuntime *rt = MBJS_NewRuntime();

/* 2. embedder-owned arena ("parameter diambil"): you hold it, you audit it,
      you destroy it. */
MBJSMemArena a;
mbjs_mem_arena_init(&a, 0);
MBJSRuntime *rt = MBJS_NewRuntime2(mbjs_mem_mf(), &a);
/* ... */
MBJS_FreeRuntime(rt);                       /* rt memory stays in the arena */
MBJSMemAudit audit = mbjs_mem_arena_audit(&a);  /* leak audit */
mbjs_mem_arena_destroy(&a);                 /* one bulk reclaim (used == 0) */
```

`MBJS_GetSunatStats(rt, &st)` reports `gc_runs` (always 0), `arena_used` /
`arena_peak` / `arena_limit`, and the engine-accounted bytes/allocs.
`MBJS_GetSunatArena(rt)` returns the backing arena (validated by magic).

Build & verify:

```sh
make sunat       # dist/sunat/{libmochiberry.so,.a,.o,mbjs-sunat}
make sunattest   # 26 checks: GC never runs, cycles, churn, bulk reclaim
```

The sunat library exports the same `MBJS_*` API as the normal build but with
different memory semantics — link it **instead of**, never together with,
the normal `libmochiberry`.

## Berry OS

`platform/berry/` embeds MBJS on Berry-OS-class devices (ESP32, STM32, ...):

- `mbjs_berry.h/c` — boots the engine on the arena with configurable heap and
  stack budgets (`berry_config.h`). `mbjs_berry_eval_binary()` runs a compiled
  bytecode blob (the OTA `app.mbc` format).
- Apps ship as **bytecode**; the device runs `mbjs_std_eval_binary()`. No
  compiler on the device.
- The engine ships as `libmochiberry.so` with a stable `MBJS_*` ABI, so the
  firmware that embeds the loader never needs re-flashing for app updates.

### MBJS in BeASM — native Berry OS binaries

`scripts/build-berry.sh <app.js>` puts MBJS **inside** the Berry assembler
toolchain (the repo builds `beasm` from `beasm/beasm.c` with `make beasm`):

1. `mbjsc -b` compiles the app to bytecode; the blob is embedded in a
   `.basm` module as `db` data.
2. `BERRY_IMPORT mbjs_berry_init, mbjs_berry_eval_binary, ...` declares the
   MBJS runtime entry points; beasm turns the `dq` references into
   `.cherryreva` relocations, so the Berry OS loader links the module
   against the MBJS runtime at load time.
3. An exported `mbjs_app_entry` (written in beasm) boots the engine on the
   arena, runs the embedded bytecode and tears down — the app runs natively
   on Berry OS.
4. The **same module** is converted to GNU as (`scripts/basm-to-gas.sh`),
   linked against the real engine and executed on the host — the exact
   binary Berry OS will load is proven to run here:

```sh
make beasm    # build the Berry assembler host tool
make berry    # build + verify app_mochiberry, aot_demo, bench_fib
# dist/berry/<app>.basm   readable Berry module
# dist/berry/<app>.bex    BEXI64 Berry OS binary (4 sections, 1 export, 4 relocs)
# dist/berry/<app>.host   host-verification binary (same module, runs the app)
```

`make berry` output matches the interpreter exactly
(`app: fib(20) = 6765`, `bench total = 100003427860`, ...).

## Bare metal / custom OSes

`platform/baremetal/` boots the whole engine inside **one caller-owned RAM
region** — no OS, no allocator:

- `mbjs_baremetal.h/c` — `mbjs_baremetal_init(&bm, region, size, limit)`
  seeds the arena with the region and creates the runtime/context on it;
  `destroy()` rewinds the region for the next boot.
- `link.ld` — ARM Cortex-M linker script template with an `.mbjs_heap`
  section.
- `make baremetal-check` compiles the memory foundation with
  `-DMBJS_MEM_NO_LIBC -ffreestanding -nostdlib` and asserts `nm` finds no
  malloc/free/memcpy/memset symbols, plus a `-Wall -Wextra` check of the
  adapter.

```sh
make baremetal-check
```

See `platform/baremetal/README.md` for toolchain examples (arm-none-eabi,
riscv32-unknown-elf) and the linker script wiring.

## OTA — upgrades without re-coding

`sh scripts/make-ota.sh [version] [app.js]` produces `dist/ota/<version>/`:

```
manifest.json          # contract (see ota/manifest.schema.json)
SHA256SUMS             # checksums for every artifact
libmochiberry.so       # the engine
app.mbc                # the app, compiled to bytecode (mbjsc -b)
patches/patch_*.js     # pure-JS ES2025/2026 + V8-compat feature patches
```

plus a single zip. The device verifies hashes, checks the `abi` tag, swaps
files, and falls back to the previous bundle on boot failure. Apps, engine
and language features each upgrade independently — see `ota/README.md`.

## ES2025 / ES2026 + V8 compatibility patches

### What ECMAScript 2026 (edition 17) actually contains

Researched against the TC39 finished-proposals track for the June 2026
edition. ES2026 (ES17) ships **seven** new proposals, all of which MBJS
implements — natively in this 2026 fork, or as self-skipping pure-JS
patches on older builds:

| proposal | spec | MBJS |
| -------- | ---- | ---- |
| `Math.sumPrecise` | ES2026 | native |
| `Uint8Array` base64/hex (`toBase64`/`toHex`/`fromBase64`/`fromHex`, `setFrom*`) | ES2026 | native |
| `Error.isError` | ES2026 | native |
| `Iterator.concat` | ES2026 | native |
| `Map.getOrInsert` / `Map.getOrInsertComputed` (Upsert) | ES2026 | native |
| `Array.fromAsync` | ES2026 | native |
| `JSON.parse` source-access (`JSON.rawJSON` etc.) | ES2026 | patched |

Not in ES2026: `Temporal` and `using` are ES2027; `import defer` is still
Stage 3. `String.dedent` (Stage 3) and an `Intl` shim are also patched in
for completeness. `make patchtest` runs the full ES2026 conformance suite
(84 checks, including every proposal above).

### The patch layer

`patches/patch_*.js` are pure-JS (helpers use the `js_` prefix) and
self-skipping: features the engine already has natively are reported as
`native` and left untouched. Current report:

```
$ build/mbjs -m patches/patch_loader.js
  native   Promise.try, RegExp.escape, Set.prototype.*, Math.f16round
  native   Error.isError, Array.fromAsync, Iterator helpers, ...
  patched  JSON.parse source-access, String.dedent
  patched  structuredClone, TextEncoder, TextDecoder
  patched  timers (setTimeout/setInterval via mbjs:os)
  patched  URL, URLSearchParams, crypto.getRandomValues, fetch (stub)
```

`patch_v8compat.js` closes the V8/Node *runtime* gap (V8 syntax is standard
ECMA-262, which the engine implements): `structuredClone`, UTF-8
`TextEncoder`/`TextDecoder`, timers, `URL`/`URLSearchParams`,
`crypto.getRandomValues` (non-secure shim — replace with a hardware RNG on
devices) and a rejecting `fetch` stub.

### V8 syntax parity battery

`tests/test_v8syntax.js` (`make v8test`) is a self-checking sweep of the
modern syntax surface a V8/Node-style engine must parse and run:
destructuring, spread, tagged templates, generators, async/await,
for-await-of, classes with fields + private members + static blocks,
optional chaining, nullish coalescing, logical assignment, `BigInt`,
`Proxy`, `WeakRef`, `.at()`, `findLast`, `Object.hasOwn`, `Error` cause,
`Object.groupBy`, `Uint8Array` hex/base64, `Math.sumPrecise`,
`Promise.try`, regexp named groups with the `d` flag, plus the
patch-provided `structuredClone`/`TextEncoder`/`TextDecoder` — every check
asserts its result and the process exits non-zero on any failure.

## Testing the shared library against a JS app

```sh
sh scripts/build-libmochiberry.sh      # dist/libmochiberry.{so,a,o,pc} + host
dist/embed_libmochiberry examples/app_mochiberry.js
dist/embed_libmochiberry dist/ota/0.1.0/app.mbc   # bytecode (OTA) path
```

The "simple machine" host links `libmochiberry.so`, boots the engine on the
arena with a 64 MiB budget and a 32 MiB JS heap limit, runs the app, drains
the event loop, and audits the arena for leaks on exit.

## Env vars / setup

- **None required.** The engine is self-contained (C + CMake only).
- The Freebuff preview runs `node server.js` (listens on `$PORT`, binds
  `0.0.0.0`); it serves a landing page and evaluates JavaScript live through
  `build/mbjs`. Build the engine once with `make` (the repo's npm `install`
  script does this automatically).
- `MBJS_DUMP_FLAGS` (see `mbjs.man`) enables engine debug dumps.
- `CC` selects the compiler for the JIT backend (`mbjs_jit`) and for the
  native/AOT builder.

## License & thanks

MIT — see `LICENSE`. This fork builds on the work of Fabrice Bellard, Charlie
Gordon, Ben Noordhuis, Saúl Ibarra Corretgé and the QuickJS community.

[ECMAScript]: https://tc39.es/ecma262/
