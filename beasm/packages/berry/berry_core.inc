; ═══════════════════════════════════════════════════════════════════════════
; berry_core.inc — Berry Assembler Core Macros
; ═══════════════════════════════════════════════════════════════════════════
;
; Makro inti untuk semua format Berry (BEFB, BEX, BDL, LBRCT, HBRCT, BSP, BO).
; Sertakan file ini di awal setiap .basm:
;
;   include 'berry_core.inc'
;
; GRAMMAR SINKRON DENGAN KERNEL (kernel/include/{bdl,befb,bex,rtcf,bsp}_format.h):
;   - magic 8-byte: BEFB164 / BEXI64 / BDLI64 / RTCFI64 / BSP164 (LE)
;   - tabel seksi 16 B/entri { type u32, offset u32, size u32, vaddr u32 }
;   - entri .exports 12 B { fn_hash u32, entry_off u32, flags u16, rsv u16 },
;     terminator {0,0,0,0}, entry_off = RVA dari .text base (TIDAK di-patch)
;   - entri CherryReva 8 B { offset u32, type u8, width u8, rsv u16 }
;
; ═══════════════════════════════════════════════════════════════════════════

; ── Target Architecture ──────────────────────────────────────────────────
; Default: x86_64. Override dengan:  TARGET_ARCH equ x86_64
; Supported: x86_16, i386, x86_64, arm32, arm64, riscv32, riscv64
if ~ defined TARGET_ARCH
    TARGET_ARCH equ x86_64
end if

; ── Magic Constants (8-byte, LE) — sama dengan kernel ────────────────────
; BEFB164  = 'B','E','F','B','1','6','4',0
; BEXI64   = 'B','E','X','I','6','4',0,0
; BDLI64   = 'B','D','L','I','6','4',0,0
; RTCFI64  = 'R','T','C','F','I','6','4',0
; BSP164   = 'B','S','P','1','6','4',0
; BOBJ     = 'B','O','B','J',0,0,0,0

; ── Export Flags ─────────────────────────────────────────────────────────
BERRY_SHARED equ 1   ; Bisa dipakai oleh Aplikasi Ring-3 & Modul Ring-0
BERRY_SYSTEM equ 2   ; HANYA bisa dipakai oleh Kernel / Modul Ring-0
BERRY_PRIVATE equ 0  ; Tidak diekspor

; ── Section Types (IDENTIK kernel/include/bdl_format.h) ──────────────────
BERRY_SECT_TEXT       equ 0
BERRY_SECT_RODATA     equ 1
BERRY_SECT_DATA       equ 2
BERRY_SECT_BSS        equ 3
BERRY_SECT_CHERRYREVA equ 4
BERRY_SECT_TRUST      equ 5
BERRY_SECT_EXPORTS    equ 6   ; BDL/RTCF. BEFB/BEX: exports = 9 (6 = .icon0)
BERRY_SECT_HOOK       equ 7
BERRY_SECT_MANIFEST   equ 8

; ═══════════════════════════════════════════════════════════════════════════
; FNV-1a 32-BIT HASH — Compile-Time Computation
; ═══════════════════════════════════════════════════════════════════════════
;
; Menghitung hash FNV-1a dari string secara otomatis saat kompilasi.
; Identik kernel bdl_fnv1a() / tools/*.js.
;
; Contoh:
;   fnv1a_32 my_hash, 'bsnd_dsp_polish'
;   ; my_hash sekarang = 0x4c3e463c (BUKAN 0xdac00005!)

macro fnv1a_32 result, string {
    local hash_val, char_byte, idx
    hash_val = 0x811c9dc5
    idx = 0
    while idx < lengthof string
        char_byte = string[idx]
        hash_val = hash_val xor char_byte
        hash_val = (hash_val * 0x01000193) and 0xFFFFFFFF
        idx = idx + 1
    end while
    result = hash_val
}

; ── Variant: FNV-1a 64-bit ──
macro fnv1a_64 result, string {
    local hash_val, char_byte, idx
    hash_val = 0xcbf29ce484222325
    idx = 0
    while idx < lengthof string
        char_byte = string[idx]
        hash_val = hash_val xor char_byte
        hash_val = (hash_val * 0x100000001b3) and 0xFFFFFFFFFFFFFFFF
        idx = idx + 1
    end while
    result = hash_val
}

; ═══════════════════════════════════════════════════════════════════════════
; BERRY_EXPORT — Otomatis Ekspor Simbol dengan Hash
; ═══════════════════════════════════════════════════════════════════════════
;
; Entri .exports = 12 B (kernel bb_bdl_export):
;   dd fn_hash     ; FNV-1a 32-bit nama simbol
;   dd entry_off   ; RVA u32 dari .text base (BUKAN pointer 64-bit!)
;   dw flags       ; 1 = SHARED, 2 = SYSTEM
;   dw 0           ; reserved
;
; Contoh:
;   bsnd_dsp_polish:
;       ; ... kode audio DSP ...
;       ret
;   BERRY_EXPORT bsnd_dsp_polish, BERRY_SHARED

macro BERRY_EXPORT func_name*, flags*=BERRY_SHARED {
    local func_hash
    fnv1a_32 func_hash, `func_name

    section '.exports'
    dd func_hash            ; FNV-1a 32-bit hash
    dd func_name            ; RVA u32 dari .text base
    dw flags                ; 1=Shared, 2=System
    dw 0                    ; reserved (12 B/entri — cocok kernel)
}

; ── Terminator .exports: { fn_hash=0, entry_off=0, flags=0, rsv=0 } ──
macro BERRY_EXPORTS_END {
    section '.exports'
    dd 0
    dd 0
    dd 0
}

; ═══════════════════════════════════════════════════════════════════════════
; SECTION MANAGEMENT
; ═══════════════════════════════════════════════════════════════════════════

; Section named berdasarkan format Berry
macro berry_section name, type {
    section name
}

; ═══════════════════════════════════════════════════════════════════════════
; CHERRYREVA — Relocation Entry (8 B/entri — kernel bb_cherryreva_entry)
; ═══════════════════════════════════════════════════════════════════════════

; Tambahkan relocation entry di section .cherryreva
; offset = posisi patch RELATIF .text base
; type   = 0 (ABS), 1 (REL)
; width  = 4 (32-bit) atau 8 (64-bit)
macro cherryreva_offset offset, type=0, width=8 {
    section '.cherryreva'
    dd offset
    db type
    db width
    dw 0    ; reserved
}

; ═══════════════════════════════════════════════════════════════════════════
; UTILITY MACROS
; ═══════════════════════════════════════════════════════════════════════════

; Null-terminated string
macro dbz string {
    db string, 0
}

; Aligned to N bytes
macro align_n n {
    db (n-1) - (($-$$) and (n-1)) dup 0
}

; ═══════════════════════════════════════════════════════════════════════════
; CONDITIONAL ARCHITECTURE INCLUSION
; ═══════════════════════════════════════════════════════════════════════════
; Otomatis include file arsitektur yang sesuai berdasarkan TARGET_ARCH

match =x86_16, TARGET_ARCH {
    include 'arch/x86/x86_16.inc'
}

match =i386, TARGET_ARCH {
    include 'arch/x86/i386.inc'
}

match =x86_64, TARGET_ARCH {
    include 'arch/x86/x86_64.inc'
}

match =arm32, TARGET_ARCH {
    include 'arch/arm/arm32.inc'
}

match =arm64, TARGET_ARCH {
    include 'arch/arm/arm64.inc'
}

match =riscv32, TARGET_ARCH {
    include 'arch/riscv/rv32.inc'
}

match =riscv64, TARGET_ARCH {
    include 'arch/riscv/rv64.inc'
}
