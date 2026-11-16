# MBJS on bare metal / custom OSes (libmochiberry)

MBJS is designed to boot on hardware that has **no OS and no allocator** —
ESP32/STM32 bare-metal, RISC-V MCUs, or a hobby kernel. The engine's memory
management is deliberately "kosongan" (empty/bare): a fixed RAM region, a
naked bump arena, explicit ownership, no GC for the C side.

## What is in this directory

| file | purpose |
|------|---------|
| `mbjs_baremetal.h/c` | bootstrap: engine inside one caller-owned RAM region |
| `baremetal_config.h` | region/stack defaults, override with `-D` |
| `link.ld` | ARM Cortex-M linker script template with an MBJS heap section |

## How it works

```
linker script (or your OS) provides a RAM region
        |
        v
mbjs_baremetal_init(&bm, region, region_size, heap_limit)
        |
        +-- mbjs_mem_arena_init_region()   <-- fixed region, no malloc
        +-- MBJS_NewRuntime2(mbjs_mem_mf(), &arena)   <-- EVERY engine byte
        +-- MBJS_NewContext(rt)
```

- The arena hands out every allocation from the region: engine C objects,
  the JS heap, bytecode buffers, ownership-marked handle records.
- Exhaustion returns `NULL` — the engine fails cleanly instead of faulting.
- `mbjs_baremetal_destroy()` rewinds the whole region: the same RAM can boot
  a fresh runtime, which is the isolation model a custom OS wants.
- std/os/console helpers are **not** initialized — there is no filesystem or
  clock on bare metal. The embedder installs its own `print`/console hooks
  on the context.

## Building without a libc (the memory foundation)

The memory foundation itself compiles with no libc at all:

```sh
cc -DMBJS_MEM_NO_LIBC -ffreestanding -nostdlib -I. -c mbjs-mem.c -o mbjs-mem.o
nm mbjs-mem.o        # must show NO malloc/free/memcpy/memset references
```

`make baremetal-check` runs exactly this check plus a freestanding syntax
check of the adapter. The engine core (`mbjs.c`) still needs a C library
(newlib / picolibc) on the target — a toolchain concern, not a runtime one.

## Cross-compiling

`sh scripts/cross-build.sh` builds `libmochiberry.a`/`.o` for arm32, arm64,
riscv32 (bare-metal ELF), riscv64, x86_32 and x86_64. For an MCU:

```sh
# ARM Cortex-M (thumb), freestanding, hard float
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
    -DMBJS_MEM_NO_LIBC -ffreestanding -Os \
    -c mbjs-mem.c platform/baremetal/mbjs_baremetal.c ...

# RISC-V RV32IMAC bare-metal
riscv32-unknown-elf-gcc -march=rv32imac -mabi=ilp32 -ffreestanding -Os \
    -c mbjs-mem.c platform/baremetal/mbjs_baremetal.c ...
```

Link with `platform/baremetal/link.ld` (adjust FLASH/RAM to your MCU) and
seed the engine with the `.mbjs_heap` section:

```c
extern char __mbjs_heap_start[];
MBJSBaremetal bm;
mbjs_baremetal_init(&bm, __mbjs_heap_start,
                    sizeof_region, 0 /* whole region */);
```

## OTA on bare metal

The OTA pipeline (`sh scripts/make-ota.sh`) produces `app.mbc` bytecode; on
bare metal the bytecode is evaluated with `MBJS_Eval(..., MBJS_EVAL_TYPE_GLOBAL)`
after loading — the same swap-in-place upgrade model as Berry OS, no
re-compilation on device.
