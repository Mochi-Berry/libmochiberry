# 🍓 beasm — Berry Assembler

**Self-hosting assembler for UBerryNix** — reads `.basm` source and produces native Berry binaries that exactly match the kernel binary grammar (`kernel/include/{bdl,befb,bex,rtcf,bsp,bpp}_format.h`).

---

## 📦 Supported Output Formats

All formats follow the **kernel binary grammar** with consistent section tables (16 bytes/entry) and export entries (12 bytes/entry).

| Format | Magic | Header Size | Purpose |
|--------|-------|-------------|---------|
| **`.befb`** | `BEFB164\0` | 36 B | Berry Executable — Ring-0 isolated applications (LBRT) |
| **`.bex`** | `BEXI64\0\0` | 16 B | Berry Executable — Nano Kernel / Recovery |
| **`.bdl`** | `BDLI64\0\0` | 36 B | Berry Dynamic Library |
| **`.lbrct` / `.hbrct`** | `RTCFI64\0` | 12 B | Runtime Component (bundle) |
| **`.bsp`** | `BSP164\0\0` | 24 B | Berry System Package (Scheduler) |
| **`.bpp`** | `BPP164\0\0` | 24 B | Berry Panel Program — GUI App (DE/WM/DS) |
| **`.bo`** | `BOBJ\0\0\0\0` | 16 B | Berry Object (intermediate format) |

### Common Format Features

All formats share:
- **Section table**: 16 B/entry `{ type u32, offset u32, size u32, vaddr u32 }`
- **`.exports` table**: 12 B/entry `{ fn_hash u32, entry_off u32, flags u16, rsv u16 }`
  - `entry_off` = RVA from `.text` base (NOT patched by CherryReva)
  - Terminator: `{0, 0, 0, 0}`
- **CherryReva entries**: 8 B/entry `{ offset u32, type u8, width u8, rsv u16 }`
  - `offset` = patch position relative to `.text` base

---

## 🛠️ Build

### Build on Host (Linux/macOS)
```bash
# Requires libc for file I/O
gcc -O2 -o build/beasm distro/base/runtime/beasm/beasm.c
```

### Self-Hosting Build (in UBerryNix)
```bash
# Once beasm is running inside the OS
beasm beasm.c -o beasm.befb
```

---

## 🚀 Usage

### Basic Assembly
```bash
# Build BEX (nano kernel executable)
beasm examples/hello.basm -o hello.bex --format bex --entry _start

# Build BEFB (LBRT application)
beasm examples/hello.basm -o hello.befb --format befb --entry _start --token BEFB_00001

# Build BDL (dynamic library)
beasm examples/mathlib.basm -o mathlib.bdl --format bdl --token BDL_00000

# Build BPP (GUI application)
beasm examples/hello_bpp.basm -o hello_bpp.bpp --format bpp --win-w 320 --win-h 200
```

### Hash Utilities
```bash
# Compute FNV-1a hash (synchronized with kernel bdl_fnv1a)
beasm --hash bsnd_dsp_polish   # 0x4c3e463c (NOT 0xdac00005!)
beasm --hash bdl_hello         # 0x6b7a5c86
```

### Command-Line Options
| Option | Description |
|--------|-------------|
| `-o <file>` | Output file name |
| `--format <type>` | `befb|bex|bdl|lbrct|hbrct|bsp|bpp|bo` |
| `--token <BTP>` | Berry Token Protocol ID |
| `--entry <sym>` | Entry point symbol |
| `--version <1\|2>` | Format version |
| `--win-w <px>` | Window width (BPP only) |
| `--win-h <px>` | Window height (BPP only) |
| `--hash <name>` | Compute FNV-1a hash of name |

---

## 🧪 Verification

```bash
# Validate binary against kernel grammar
node tools/beasm_check.js hello.bex
node tools/beasm_check.js mathlib.bdl
```

Checks performed:
- 8-byte magic
- Header size
- BTP token format
- Section table (16 B/entry)
- `.exports` table (12 B/entry) with terminator
- CherryReva entries (8 B/entry)

---

## ⚙️ Supported Instructions (x86_64)

### Core ALU & Control Flow
- `mov` (reg/imm/mem, movabs, cr/dr)
- `lea`, `push`/`pop`, `call`/`jmp`/`jcc` (rel32)
- `ret`, `int`/`int3`/`syscall`/`sysret`/`sysenter`/`sysexit`
- `hlt`/`nop`/`ud2`/`cli`/`sti`/`cld`/`std`
- `add`/`adc`/`sbb`/`or`/`and`/`sub`/`xor`/`cmp`/`test`
- `inc`/`dec`/`neg`/`not`, `imul`
- `shl`/`shr`/`sar`/`rol`/`ror`, `shld`/`shrd`
- `movzx`/`movsx`, `xchg`, `setcc`
- `cdq`/`cqo`/`cwde`/`cdqe`, `leave`, `pushfq`/`popfq`

### Ring-0 / Bare-Metal Instructions

| Category | Instructions |
|----------|--------------|
| **I/O Port** | `in`/`out` (al/ax/eax ↔ dx/imm8) |
| **MSR & TSC** | `rdmsr`/`wrmsr`, `rdpmc`, `rdtsc`/`rdtscp`, `cpuid` |
| **System Control** | `xgetbv`/`xsetbv`, `clac`/`stac`, `swapgs`, `wbinvd`/`invd`, `invlpg` |
| **Interrupt** | `iretq`, `int`/`int3`, `syscall`/`sysret` |
| **String Ops** | `movsb/w/d/q`, `stosb/w/d/q`, `lodsb/w/d/q`, `cmpsb/w/d/q`, `scasb/w/d/q`, `insb/w/d`, `outsb/w/d` (with `rep`/`repe`/`repne`) |
| **Descriptor Tables** | `lgdt`/`lidt`/`sgdt`/`sidt`, `lldt`/`ltr`/`sldt`/`str` |
| **Cache Control** | `clflush`/`clflushopt`, `prefetcht0/t1/t2/nta` |
| **Atomic (lock)** | `cmpxchg`, `cmpxchg8b`/`cmpxchg16b`, `xadd` |
| **Bit Manipulation** | `bt`/`bts`/`btr`/`btc`, `bsf`/`bsr`, `bswap` |
| **Control/Debug Regs** | `mov cr0..cr15, r64` / `mov r64, cr0..cr15`, `mov dr0..dr15, r64` / `mov r64, dr0..dr15` |
| **Loop** | `loop`/`loope`/`loopne`, `jrcxz`/`jecxz` (rel8) |

### Operands Supported
- Registers: 8/16/32/64-bit (including UBerry aliases `r0`..`r22`)
- Control/debug registers: `cr0..cr15`, `dr0..dr15`
- Immediate values
- Memory addressing: `[reg]`, `[reg+disp]`, `[reg+idx*scale+disp]`, `[rip+disp]` / `[rel sym]`, `[disp32]`
- Directives: `section`, `db`/`dw`/`dd`/`dq`, `equ`/`=`, `align`, `resb`/`resw`/`resd`/`resq`
- Labels: `name:` and implicit `name db ...`
- `lock` prefix
- Automatic relocation: references to labels generate CherryReva entries (ABS 4/8 B)
- RIP-relative references do NOT generate relocations

**Complete ring-0 example**: `examples/sys_ring0.basm`

---

## 📁 Project Structure

---

## 🔧 Multi-Architecture Support

BeASM supports **7 architectures** via the `.arch` directive. Each architecture has its own instruction set, register file, and encoding.

### Supported Architectures

| Architecture | `.arch` name | Registers | Syscall | Status |
|--------------|-------------|-----------|---------|--------|
| **x86_64** | `x86_64`, `amd64`, `x64` | 8/16/32/64-bit GPR | `syscall` | ✅ Full |
| **x86_32** | `x86_32`, `i386`, `x86` | 8/16/32-bit GPR | `int 0x80` | ✅ Full |
| **x86_16** | `x86_16`, `i8086`, `real` | 8/16-bit GPR | `int 21h` | ✅ Full |
| **ARM32** | `arm32`, `arm`, `a32` | r0-r15, sp, lr, pc | `svc`/`swi` | ✅ Full |
| **ARM64** | `arm64`, `aarch64`, `a64` | x0-x30, w0-w30, xzr/sp | `svc` | ✅ Full |
| **RISC-V 32** | `riscv32`, `rv32`, `rv32i` | x0-x31 (a0-a7, s0-s11) | `ecall` | ✅ Full |
| **RISC-V 64** | `riscv64`, `rv64`, `rv64i` | x0-x31 + W-instructions | `ecall` | ✅ Full |
| **RISC-V 128** | `riscv128`, `rv128`, `rv128i` | x0-x31 + D-instructions | `ecall` | ✅ Full |

### ARM32 Instructions (A32)
- **ALU**: `mov`, `mvn`, `add`, `sub`, `rsb`, `and`, `orr`, `eor`, `bic`, `tst`, `teq`, `cmp`, `cmn`
- **Shift**: `lsl`, `lsr`, `asr`, `ror`
- **Multiply**: `mul`, `mla`, `umull`, `smull`
- **Load/Store**: `ldr`, `str`, `ldrb`, `strb`, `ldrh`, `strh`
- **Branch**: `b`, `bl`, `bx`, `blx`
- **System**: `swi`/`svc`, `mrs`, `msr`, `nop`, `wfi`, `wfe`, `sev`, `isb`, `dsb`, `dmb`
- **Other**: `clz`, `push`, `pop`, `ret` (= `bx lr`)

### ARM64 Instructions (A64)
- **ALU**: `add`, `sub`, `mov`, `orr`, `and`, `eor`, `cmp`, `cmn`
- **Load/Store**: `ldr`, `str`, `ldp`, `stp`
- **Branch**: `b`, `bl`, `br`, `blr`, `ret`, `b.cond`
- **System**: `svc`, `nop`, `mrs`, `msr`, `adrp`, `wfi`, `wfe`, `sev`, `isb`, `dsb`, `dmb`
- **Registers**: `x0-x30` (64-bit), `w0-w30` (32-bit), `xzr`, `sp`, `lr`, `fp`

### RISC-V Instructions (RV32I/M/C + RV64I + RV128I)
- **ALU**: `add`, `sub`, `addi`, `and`, `andi`, `or`, `ori`, `xor`, `xori`
- **Shift**: `sll`, `srl`, `sra`, `slli`, `srli`, `srai`
- **Compare**: `slt`, `slti`, `sltu`, `sltiu`
- **Load/Store**: `lb/lh/lw/ld/lbu/lhu/lwu`, `sb/sh/sw/sd`
- **Branch**: `beq`, `bne`, `blt`, `bge`, `bltu`, `bgeu`
- **Jump**: `jal`, `jalr`, `j`, `jr`, `call`, `tail`
- **Upper Imm**: `lui`, `auipc`
- **M ext**: `mul`, `mulh`, `mulhsu`, `mulhu`, `div`, `divu`, `rem`, `remu`
- **System**: `ecall`, `ebreak`, `fence`, `rdcycle`, `rdtime`, `rdinstret`
- **CSR**: `csrrw`, `csrrs`, `csrrc`, `csrrwi`, `csrrsi`, `csrrci`
- **RV64I**: `addw`, `subw`, `addiw`, `sllw`, `srlw`, `sraw`, `ld`, `sd`, `lwu`
- **RV128I**: `addd`, `subd`, `lq`, `sq`, `ldu`
- **Pseudo**: `nop`, `mv`, `not`, `neg`, `li`, `la`, `ret`, `sext.w`

### Usage Example

```asm
arch arm64
section .text

_start:
    mov x0, 42
    mov x1, 58
    add x2, x0, x1
    svc 0x900001
```



```
beasm/
├── beasm.c                    # Main assembler (two-pass x86_64, kernel grammar)
├── examples/
│   ├── hello.basm             # .bex + .befb (syscall SOW/WITHER)
│   ├── mathlib.basm           # .bdl (exports + CherryReva relocation)
│   ├── demo_api.basm          # 3 API pillars (nano_call / lbrt_invoke / gui_send)
│   ├── hello_bpp.basm         # .bpp GUI app (hook table + service table)
│   └── sys_ring0.basm         # Full ring-0 instruction verification
└── packages/berry/            # FASM/G macros (for host fasmg users)
    ├── berry_core.inc         # Core macros (FNV-1a, BERRY_EXPORT 12 B, section)
    ├── format/                # bdl/befb/bex/lbrct/bsp/bpp/bo — kernel grammar
    ├── api/                   # nano_abi / lbrt_api / gui_api / berryio (modular)
    └── arch/                  # x86_16, i386, x86_64, arm32, arm64, rv32, rv64
```

---

## 🔌 BerryIO — Modular API Library

`packages/berry/api/berryio.inc` wraps the ENTIRE BerryCall contract (`kernel/include/berrycall.h`) into a single `berryio_*` namespace — self-contained (includes `lbrt_api.inc` internally).

### Available Functions

| Category | Functions |
|----------|-----------|
| **I/O** | `berryio_io_write/read/open/close/stat/ctl/exec` |
| **Filesystem** | `berryio_fs_read/perm/sync` |
| **Process** | `berryio_proc_getpid/list` |
| **Time** | `berryio_time_tick/sleep` |
| **Memory** | `berryio_mem_alloc/free` |
| **Components** | `berryio_comp_reg/call/sym`, `berryio_bpp_load/mint` |
| **Display** | `berryio_dsp_query/modes/set/text/canvas/canvas_free/flip/session` |
| **Window** | `berryio_win_create/destroy/move/resize/focus/frame/input/list/workspace` |
| **Activity** | `berryio_act_reg/unreg/query/bump/drop/list` |
| **Storage** | `berryio_stor_persist/bdl_load/lbrct_load` |
| **Panic** | `berryio_panic_trigger/version` |

All constants and numbers are 100% synchronized with `berrycall.h` (verified by `tools/audit_sync.js`).

### Language Bindings
- **FASM/G** (`packages/berry/`): `berryio_*` macros + `bc_*`/`gui_*` (legacy)
- **C/C3**: `libnullberry` (ubosix.h/c) + `develop/c3/berryio` + `develop/c3/ubosix`

---

## 🔀 Two Development Paths

### Path 1: `beasm.c` (Primary, Self-Hosting)
Standalone C assembler that runs **inside UBerryNix**. Source `.basm` is plain without `include 'berry_core.inc'` — syscall constants defined via `equ` in source. Examples in `examples/` use this path.

### Path 2: FASM/G Macro Packages (Host)
For `fasmg` (Flat Assembler G) users on host:
```assembly
include 'berry_core.inc'
BERRY_EXPORT my_func, BERRY_SHARED
nano_call SYS_MEM_ALLOC
lbrt_invoke 'bsnd_dsp_polish'
gui_send GUI_DRAW_TEXT
```

Macros compute FNV-1a hashes **at compile time**. The grammar is **IDENTICAL** to `beasm.c` (synchronized with kernel).

> **FNV-1a Hash**: `0x811c9dc5` / `0x01000193` — identical across kernel (`bdl_fnv1a`), `berryhashlay.c`, `bdl_packer.js`, `beasm.c`, and FASM/G macros. The `0xdac0xxxx` values in older docs were placeholders — actual hash of `bsnd_dsp_polish` = `0x4c3e463c`.

---

## 🔍 `berryhashlay` — Export Table Generator

A native C scanner that auto-generates export tables from C source code.

### Build
```bash
cc -o build/berryhashlay tools/berryhashlay.c
```

### Usage
```bash
# Scan directory and generate export table
build/berryhashlay -o build/libberrysound_exports.gen.h \
    --scan-dir distro/base/component/lowlevel/libberrysound/src
```

### Integration
Automatically integrated into the Makefile as a pre-build step for components.

---

## 🏗️ Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                   Source (.basm)                            │
│  - section / db..dq / equ / label                           │
│  - BERRY_EXPORT sym, BERRY_SHARED                           │
│  - mov/call/jcc/syscall (x86_64)                            │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                 beasm.c (Assembler)                         │
│  - FNV-1a hash compile-time                                 │
│  - Two-pass: size → vaddr → emit                            │
│  - CherryReva relocation (ABS 4/8 B)                        │
│  - Format header + section table (kernel grammar)           │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│   Binary Output (.befb/.bex/.bdl/.lbrct/.bsp/.bpp/.bo)      │
│   magic 8-byte + header + sect 16 B + exports + CherryReva  │
└─────────────────────────────────────────────────────────────┘
```

---

## 🤝 Contributing

1. **Add instruction support**: Extend the parser in `beasm.c`
2. **Add format**: Add a new format handler in `packages/berry/format/`
3. **Sync with kernel**: Run `tools/audit_sync.js` to verify grammar alignment
4. **Test**: Use `tools/beasm_check.js` to validate output

---

## 📜 License

Part of UBerryNix Core Systems — see root LICENSE.

---

<div align="center">

**Made with 🍓 by Berry Chan — "Null Berry Chan"**  
*Everything is a token. Nothing is copied twice.*

</div>