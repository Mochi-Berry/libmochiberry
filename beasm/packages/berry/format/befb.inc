; ═══════════════════════════════════════════════════════════════════════════
; befb.inc — Berry Executable Format Binary (.befb) — MILIK LBRT
; ═══════════════════════════════════════════════════════════════════════════
;
; GRAMMAR KERNEL (kernel/include/befb_format.h):
;   magic 8-byte "BEFB164\0\0"   (BEFB164 = CISC 64-bit)
;   header 36 B: magic(8) ns(2) ver(1) flags(1) entry(4) token(20)
;   tabel seksi 16 B/entri { type u32, off u32, size u32, vaddr u32 }
;   seksi: .text .rodata .data .bss .cherryreva .trust .icon0 .hook
;          .manifest  —  .exports = 9 (opsional)
;
; ╔══════════════════════════════════════════════════════════════════════════╗
; ║  PENTING — HIERARKI KASTA EKSEKUSI:                                    ║
; ║                                                                        ║
; ║  .befb = MILIK LBRT (Low-Level RunTime).  BEFB adalah program di ATAS   ║
; ║          nano kernel — aplikasi LBRT.  BEFB memakai BTP TANPA BATAS     ║
; ║          (token befb_xxxxx, bisa lahirkan tree process/thread).         ║
; ║                                                                        ║
; ║  BEX = KoID0/1 + BTP wrapper, thread ∞.                                ║
; ║  BEFB = LBRT + BTP ∞, BUKAN nano.                                      ║
; ╚══════════════════════════════════════════════════════════════════════════╝
;
; BEFB dipakai untuk: aplikasi Ring-3 (shell, kalkulator, GUI app, game),
; utilitas, daemon HBRT.  Multi-instance bebas: tiap buka = token baru.

; ── Magic (8-byte LE): 'B','E','F','B','1','6','4',0 ──
BEFB_MAGIC_0 equ 'B'
BEFB_MAGIC_1 equ 'E'
BEFB_MAGIC_2 equ 'F'
BEFB_MAGIC_3 equ 'B'
BEFB_MAGIC_4 equ '1'   ; ISA digit: '1' = CISC (x86)
BEFB_MAGIC_5 equ '6'
BEFB_MAGIC_6 equ '4'
BEFB_MAGIC_7 equ 0

; ── Header BEFB — 36 B (identik bb_befb_header kernel) ──
macro format_BEFB token {
    format binary
    db BEFB_MAGIC_0, BEFB_MAGIC_1, BEFB_MAGIC_2, BEFB_MAGIC_3
    db BEFB_MAGIC_4, BEFB_MAGIC_5, BEFB_MAGIC_6, BEFB_MAGIC_7
    dw 0                        ; num_sections (diisi belink/assembler)
    db 1                        ; version
    db 0                        ; flags (bit0 = escalation-capable)
    dd 0                        ; entry_point (0 = pakai .hook)
    ; ── BTP token: prefix[8] + digits(4) + seedLo(4) + seedHi(4) ──
    db token                    ; prefix (mis. 'BEFB')
    db 0,0,0,0,0,0,0,0          ; pad prefix ke 8
    dd 5                        ; digits (seed width)
    dd 0                        ; seedLo
    dd 0                        ; seedHi
}

; ── BEFB Section Types (identik kernel) ──
BEFB_SECT_TEXT       equ 0
BEFB_SECT_RODATA     equ 1
BEFB_SECT_DATA       equ 2
BEFB_SECT_BSS        equ 3
BEFB_SECT_CHERRYREVA equ 4
BEFB_SECT_TRUST      equ 5
BEFB_SECT_ICON0      equ 6
BEFB_SECT_HOOK       equ 7
BEFB_SECT_MANIFEST   equ 8
BEFB_SECT_EXPORTS    equ 9   ; export stubs ABI/API (opsional)

macro befb_section_text {
    section '.text' code executable
}

macro befb_section_rodata {
    section '.rodata' data readable
}

macro befb_section_data {
    section '.data' data readable writable
}

macro befb_section_bss {
    section '.bss' bss readable writable
}

macro befb_section_exports {
    section '.exports' data readable
}

macro befb_section_cherryreva {
    section '.cherryreva' data readable
}

; ── BTP token prefix for BEFB (identitas murni token — BUKAN KoID) ──
BTP_PREFIX_BEFB equ 'BEFB_'
