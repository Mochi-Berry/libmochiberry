# MBJS target architectures — research notes

libmochiberry cross-compiles to the **7 architecture families** listed
below. This file records the ISA facts the porting notes depend on,
researched from the references at the bottom (OSDev wiki, the ARM
AAPCS/AAPCS64 specifications, the RISC-V calling-convention specification,
the GCC IA-16 port docs and x86 reference material).

```
 x86_16   real mode 16-bit (DOS / bare-metal)
 x86_32   i386 .. i686
 x86_64   amd64
 arm32    ARMv7-A/R (AArch32)
 arm64    AArch64 (aarch64)
 riscv32  RV32 (bare-metal / embedded)
 riscv64  RV64 (Linux / bare-metal)
 (+ win64 x86_64-w64-mingw32 as a bonus target)
```

Each section lists: registers, memory model, calling convention, toolchain
prefix used by `scripts/cross-build.sh`, and the freestanding flags the
bare-metal builds use.

---

## 1. x86_16 — 16-bit real mode (DOS / bare-metal)

- **Registers**: eight 16-bit general-purpose registers — `AX BX CX DX SI
  DI BP SP` — plus eight 8-bit halves (`AH AL BH BL CH CL DH DL`) and six
  segment registers (`CS DS ES FS GS SS`). Addresses are
  `segment:offset`, 20-bit physical (1 MiB). Memory addressing in real
  mode is restricted: the base must be `BX` or `BP`, the index `SI` or
  `DI` (`[BX+SI]`, `[BP+DI]`, … — `[CX]` is invalid).
- **Modes**: real mode is the power-on state of every x86 CPU. 8086/8088/
  286 have no 32-bit anything; 386+ in real mode can still execute 32-bit
  instructions via operand-size prefixes and reach 4 GiB with address-size
  prefixes — this is what `gcc -m16` emits.
- **Calling convention**: custom for each toolchain; the GCC IA-16 port
  uses a near `cdecl`-style convention (arguments on the stack, `AX`/`DX`
  returns). No OS ABI exists; DOS programs enter at a fixed address.
- **Toolchain**: `ia16-elf-gcc` (true 8086/286 segmented output) or host
  `cc -m16 -march=i386` (386+ real mode, flat 4 GiB via prefixes).
- **MBJS**: `scripts/cross-build.sh` target `x86_16`, freestanding flags
  `-ffreestanding -nostdlib`, linker script `platform/baremetal/link16.ld`
  (flat image for a real-mode loader / DOS extender). `make x86_16-check`
  verifies the memory foundation compiles clean in `-m16` with zero libc
  references (SKIPs gracefully when the host lacks 32-bit multilib).

## 2. x86_32 — i386 .. i686

- **Registers**: eight 32-bit GPRs — `EAX EBX ECX EDX ESI EDI EBP ESP` —
  each with 16/8-bit views. Flat 4 GiB address space (protected mode).
- **Modes**: protected mode; `i386` is the 32-bit baseline, `i586`/`i686`
  add `cmpxchg8b`, `cmov`, `sysenter` etc. The engine only needs i386.
- **Calling convention**: `cdecl` on Linux/BSD (args right-to-left on the
  stack, `EAX` return); `stdcall`/`fastcall` exist on Windows.
- **Toolchain**: `i686-linux-gnu-gcc`, or host `cc -m32 -march=i386`
  (multilib) for a pure-i386 build.
- **MBJS**: cross target `x86_32_i386` / `x86_32_i686`.

## 3. x86_64 / amd64

- **Registers**: sixteen 64-bit GPRs — `RAX RBX RCX RDX RSI RDI RBP RSP
  R8..R15` — plus 8-bit views (`AL`, `SIL`, `R8B`, …) and the `RIP`
  relative addressing mode.
- **Calling convention**: SysV AMD64 on Linux/BSD — first six integer
  args in `RDI RSI RDX RCX R8 R9`, floats in `XMM0-7`, stack args after,
  callee-saved `RBX RBP R12-15`, 16-byte stack alignment, 128-byte red
  zone. Windows x64 uses `RCX RDX R8 R9` + shadow space.
- **Toolchain**: host build (native) or `x86_64-linux-gnu-gcc`.
- **MBJS**: the default development target; the JIT `cc` backend and the
  GAS/beasm emitters target this ABI.

## 4. arm32 (ARMv7-A/R, AArch32)

- **Registers**: sixteen 32-bit registers — `R0..R15` with `R13`=SP,
  `R14`=LR, `R15`=PC — plus `CPSR`/`SPSR` and the `R8-R12` banked copies
  in FIQ.
- **Calling convention**: AAPCS — args in `R0-R3`, rest on the stack;
  callee-saved `R4-R11`; `R12` = IP (scratch); `LR` holds the return
  address (no stack return-address push); 8-byte stack alignment at public
  interfaces.
- **Toolchain**: `arm-linux-gnueabihf-gcc` (hard-float ABI).
- **MBJS**: cross target `arm32`; ESP32/STM32-class Berry OS boards use
  the bare-metal adapter (`platform/baremetal`).

## 5. arm64 / AArch64

- **Registers**: 31× 64-bit GPRs `X0..X30` (`X29` = frame pointer, `X30`
  = LR; `X31`/`SP` is the stack pointer, not a GPR) with 32-bit views
  `W0..W30`; 32× 128-bit vector registers `V0..V31`.
- **Calling convention**: AAPCS64 — args in `X0-X7` (floats in `V0-V7`),
  rest on stack; callee-saved `X19-X29`; `X8` is the indirect-result
  register; 16-byte stack alignment.
- **Toolchain**: `aarch64-linux-gnu-gcc`.
- **MBJS**: cross target `arm64`.

## 6. riscv32 (RV32)

- **Registers**: 32× 32-bit registers `x0..x31`; `x0` is hardwired zero,
  `x1`=ra, `x2`=sp, `x3`=gp, `x4`=tp, `x8`=s0/fp; `x10-x17` = `a0-a7`
  (args/return), `x18-x27` = `s1-s11` (callee-saved). ILP32 integer model
  (`int` = 32 bits).
- **Calling convention**: RISC-V calling convention (riscv-calling spec) —
  args in `a0-a7`, `a0/a1` return; stack grows down, 16-byte alignment.
  The `rv32imac` bare-metal baseline is the common MCU target.
- **Toolchain**: `riscv32-unknown-elf-gcc` (bare-metal) or
  `riscv64-linux-gnu-gcc -march=rv32`.
- **MBJS**: cross target `riscv32` (bare-metal/ELF); the bare-metal
  adapter compiles freestanding with any `riscv32-unknown-elf-*`
  toolchain.

## 7. riscv64 (RV64)

- **Registers**: same 32-register layout as RV32 but 64-bit wide; LP64
  integer model (`long`/pointers = 64 bits, `int` = 32).
- **Calling convention**: same spec as RV32; `a0-a7` args, callee-saved
  `s0-s11`.
- **Toolchain**: `riscv64-linux-gnu-gcc`.
- **MBJS**: cross target `riscv64` (shared/static Linux + bare-metal).

---

## Freestanding build matrix (bare metal / custom OS)

| arch | flags | notes |
| ---- | ----- | ----- |
| x86_16 | `-m16 -march=i386 -ffreestanding -nostdlib` | 386+ real mode; ia16-elf-gcc for true 8086 |
| i386 | `-m32 -march=i386 -ffreestanding -nostdlib` | protected mode |
| arm32 | `-mcpu=cortex-m4 -ffreestanding -nostdlib` | e.g. STM32 (see `platform/baremetal/link.ld`) |
| arm64 | `-ffreestanding -nostdlib` | RPi / SBC bare metal |
| riscv32 | `-march=rv32imac -mabi=ilp32 -ffreestanding -nostdlib` | MCU class |
| riscv64 | `-march=rv64gc -mabi=lp64 -ffreestanding -nostdlib` | SBC / kernel class |

`make baremetal-check` compiles the memory foundation with
`-DMBJS_MEM_NO_LIBC` and asserts zero libc references; `make x86_16-check`
does the same for the 16-bit target. `make cross` builds all seven
families (skipping any whose toolchain is missing, with install hints).

## References

- OSDev Wiki — Real Mode: https://wiki.osdev.org/Real_Mode
- x86 registers reference: https://www.eecg.utoronto.ca/~amza/www.mindsec.com/files/x86regs.html
- OSDev Wiki — Calling Conventions: https://wiki.osdev.org/Calling_Conventions
- Wikipedia — x86 calling conventions: https://en.wikipedia.org/wiki/X86_calling_conventions
- ARM-software/abi-aa — AAPCS64: https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst
- AAPCS64 PDF: https://student.cs.uwaterloo.ca/~cs452/docs/rpi4b/aapcs64.pdf
- RISC-V calling convention (riscv.org): https://riscv.org/wp-content/uploads/2024/12/riscv-calling.pdf
- GCC IA-16 port options: https://wonderful.asie.pl/doc/gcc-ia16/gcc-6.3.0/gcc/IA_002d16-Options.html
- GCC i386 target config: https://codebrowser.dev/gcc/gcc/config/i386/i386.h.html
