; ═══════════════════════════════════════════════════════════════════════════
; bex.inc — Berry Executable eXtended (.bex) — MILIK NANO KERNEL
; ═══════════════════════════════════════════════════════════════════════════
;
; GRAMMAR KERNEL (kernel/include/bex_format.h):
;   magic 8-byte "BEXI64\0\0"   (BEXI64 = CISC 64-bit)
;   header 16 B: magic(8) ns(2) ver(1) flags(1) entry(4)
;   tabel seksi 16 B/entri { type u32, off u32, size u32, vaddr u32 }
;   seksi: .text .rodata .data .bss .cherryreva .icon0 .icon1 .hook
;          .manifest  —  .exports = 9 (opsional)
;
; ╔══════════════════════════════════════════════════════════════════════════╗
; ║  PENTING — HIERARKI KASTA EKSEKUSI:                                    ║
; ║                                                                        ║
; ║  .bex  = MILIK NANO KERNEL.  Aslinya nano kernel HANYA mengenal        ║
; ║          KoID0 (LBRT) dan KoID1 (HBRT) — DUA SLOT SAJA.  Program .bex  ║
; ║          hidup di dalam slot itu, tetapi BISA punya THREAD TANPA BATAS  ║
; ║          di dalam lingkungannya.  Akses nano kernel TIDAK langsung:     ║
; ║          .bex dibungkus (wrapped) dengan BTP token — kernel melihat     ║
; ║          BEX_xxxxx, bukan pid/koid mentah.                              ║
; ║                                                                        ║
; ║  .befb = MILIK LBRT (lihat befb.inc).                                  ║
; ╚══════════════════════════════════════════════════════════════════════════╝
;
; BEX dipakai untuk: LBRT (lowlevelrt.bex), HBRT (highlevelrt.bex), driver
; darurat, recovery tool, bootloader tahap akhir.

; ── Magic (8-byte LE): 'B','E','X','I','6','4',0,0 ──
BEX_MAGIC_0 equ 'B'
BEX_MAGIC_1 equ 'E'
BEX_MAGIC_2 equ 'X'
BEX_MAGIC_3 equ 'I'   ; ISA huruf: 'I' = CISC (x86)
BEX_MAGIC_4 equ '6'
BEX_MAGIC_5 equ '4'
BEX_MAGIC_6 equ 0
BEX_MAGIC_7 equ 0

; ── Header BEX — 16 B (identik bb_bex_header kernel) ──
;   magic(8) + num_sections u16(2) + version u8(1) + flags u8(1) + entry u32(4)
macro format_BEX {
    format binary
    db BEX_MAGIC_0, BEX_MAGIC_1, BEX_MAGIC_2, BEX_MAGIC_3
    db BEX_MAGIC_4, BEX_MAGIC_5, BEX_MAGIC_6, BEX_MAGIC_7
    dw 0                        ; num_sections (diisi belink/assembler)
    db 1                        ; version
    db 1                        ; flags: bit0 = executable
    dd 0                        ; entry_point (0 = awal .text / _start)
}

; ── BEX Section Types (identik kernel) ──
BEX_SECT_TEXT       equ 0
BEX_SECT_RODATA     equ 1
BEX_SECT_DATA       equ 2
BEX_SECT_BSS        equ 3
BEX_SECT_CHERRYREVA equ 4
BEX_SECT_ICON0      equ 5
BEX_SECT_ICON1      equ 6
BEX_SECT_HOOK       equ 7
BEX_SECT_MANIFEST   equ 8
BEX_SECT_EXPORTS    equ 9

macro bex_section_text {
    section '.text' code executable
}
