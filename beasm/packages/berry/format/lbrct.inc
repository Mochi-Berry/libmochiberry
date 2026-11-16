; ═══════════════════════════════════════════════════════════════════════════
; lbrct.inc — Low-level Berry Runtime Component (.lbrct)
; ═══════════════════════════════════════════════════════════════════════════
;
; Modul Ring-0 tanpa libc (driver, kernel modules).
; Memiliki .hook (init/start/stop) + .exports + .cherryreva.
; Berjalan di mode bare-metal absolut.
;
; GRAMMAR KERNEL (kernel/include/rtcf_format.h):
;   magic 8-byte "RTCFI64\0\0"   (RTCFI64 = CISC 64-bit)
;   header 12 B: magic(8) ns(2) ver(1) flags(1)
;   tabel seksi 16 B/entri { type u32, off u32, size u32, vaddr u32 }
;   seksi: .text .rodata .data .bss .cherryreva .trust .exports .hook
;          .manifest
;
; Layout:
;   [Header 12 B] [Section Table] [.text] [.rodata] [.data] [.bss]
;                 [.cherryreva] [.trust] [.exports] [.hook]
;
; ═══════════════════════════════════════════════════════════════════════════

; ── Magic (8-byte LE): 'R','T','C','F','I','6','4',0 ──
RTCF_MAGIC_0 equ 'R'
RTCF_MAGIC_1 equ 'T'
RTCF_MAGIC_2 equ 'C'
RTCF_MAGIC_3 equ 'F'
RTCF_MAGIC_4 equ 'I'   ; ISA huruf: 'I' = CISC (x86)
RTCF_MAGIC_5 equ '6'
RTCF_MAGIC_6 equ '4'
RTCF_MAGIC_7 equ 0

; ── Header RTCF — 12 B (identik bb_rtcf_header kernel) ──
;   magic(8) + num_sections u16(2) + version u8(1) + flags u8(1)
macro format_LBRCT {
    format binary
    db RTCF_MAGIC_0, RTCF_MAGIC_1, RTCF_MAGIC_2, RTCF_MAGIC_3
    db RTCF_MAGIC_4, RTCF_MAGIC_5, RTCF_MAGIC_6, RTCF_MAGIC_7
    dw 0                        ; num_sections (diisi belink/assembler)
    db 1                        ; version
    db 1                        ; flags: bit0 = component carries code
}

; ── RTCF Section Types (identik kernel rtcf_format.h) ──
RTCF_SECT_TEXT       equ 0
RTCF_SECT_RODATA     equ 1
RTCF_SECT_DATA       equ 2
RTCF_SECT_BSS        equ 3
RTCF_SECT_CHERRYREVA equ 4
RTCF_SECT_TRUST      equ 5
RTCF_SECT_EXPORTS    equ 6
RTCF_SECT_HOOK       equ 7
RTCF_SECT_MANIFEST   equ 8

; ═══════════════════════════════════════════════════════════════════════════
; hbrct.inc — High-level Berry Runtime Component (.hbrct)
; ═══════════════════════════════════════════════════════════════════════════
;
; Daemon layanan Ring-3 (Audio Server, Network Stack, Font Manager).
; Berjalan di latar belakang, berkomunikasi via IPC / gui_send.
;
; Container yang sama dengan .lbrct — magic RTCFI64.  Bedanya hanya
; klasifikasi layanan (Ring-3), bukan format biner.

macro format_HBRCT {
    format_LBRCT
}
