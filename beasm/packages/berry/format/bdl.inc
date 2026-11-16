; ═══════════════════════════════════════════════════════════════════════════
; bdl.inc — Berry Dynamic Library (.bdl)
; ═══════════════════════════════════════════════════════════════════════════
;
; Pustaka bersama yang hanya berisi fungsi untuk di-link dinamis.
; Tidak memiliki siklus hidup (.hook) — hanya tabel .exports.
;
; GRAMMAR KERNEL (kernel/include/bdl_format.h):
;   magic 8-byte "BDLI64\0\0"   (BDLI64 = CISC 64-bit)
;   header 36 B: magic(8) ns(2) ver(1) flags(1) entry(4) token(20)
;   tabel seksi 16 B/entri { type u32, off u32, size u32, vaddr u32 }
;   entri .exports 12 B { fn_hash u32, entry_off u32, flags u16, rsv u16 }
;
; Layout:
;   [Header 36 B] [Section Table] [.text] [.rodata] [.data] [.bss]
;                 [.cherryreva] [.trust] [.exports]
;
; ═══════════════════════════════════════════════════════════════════════════

; ── Magic (8-byte LE): 'B','D','L','I','6','4',0,0 ──
BDL_MAGIC_0 equ 'B'
BDL_MAGIC_1 equ 'D'
BDL_MAGIC_2 equ 'L'
BDL_MAGIC_3 equ 'I'   ; ISA huruf: 'I' = CISC (x86)
BDL_MAGIC_4 equ '6'
BDL_MAGIC_5 equ '4'
BDL_MAGIC_6 equ 0
BDL_MAGIC_7 equ 0

; ── Header BDL — 36 B (identik bb_bdl_header kernel) ──
;   magic(8) + num_sections u16(2) + version u8(1) + flags u8(1)
;   + entry_point u32(4) + BTP token 20 B (prefix[8]+digits u32+seedLo u32+seedHi u32)
macro format_BDL token {
    format binary
    db BDL_MAGIC_0, BDL_MAGIC_1, BDL_MAGIC_2, BDL_MAGIC_3
    db BDL_MAGIC_4, BDL_MAGIC_5, BDL_MAGIC_6, BDL_MAGIC_7
    dw 0                        ; num_sections (diisi belink/assembler)
    db 2                        ; version 2 (direktori name TANPA BATAS)
    db 1                        ; flags: hot-swappable
    dd 0                        ; entry_point (0 = pakai .hook)
    ; ── BTP token: prefix[8] + digits(4) + seedLo(4) + seedHi(4) ──
    db token                    ; prefix (mis. 'BDL')
    db 0,0,0,0,0,0,0,0          ; pad prefix ke 8
    dd 5                        ; digits (seed width)
    dd 0                        ; seedLo
    dd 0                        ; seedHi
}

; ── BDL Section Types (identik kernel) ──
BDL_SECT_TEXT       equ 0
BDL_SECT_RODATA     equ 1
BDL_SECT_DATA       equ 2
BDL_SECT_BSS        equ 3
BDL_SECT_CHERRYREVA equ 4
BDL_SECT_TRUST      equ 5
BDL_SECT_EXPORTS    equ 6
BDL_SECT_HOOK       equ 7
BDL_SECT_MANIFEST   equ 8

macro bdl_section_text {
    section '.text' code executable
}

macro bdl_section_exports {
    section '.exports' data readable
}
