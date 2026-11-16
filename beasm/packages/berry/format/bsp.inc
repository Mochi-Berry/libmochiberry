; ═══════════════════════════════════════════════════════════════════════════
; bsp.inc — Berry System Package (.bsp)
; ═══════════════════════════════════════════════════════════════════════════
;
; Paket khusus perangkat keras: scheduler plugin, ACPI tables,
; microcode CPU, inisialisasi DMA. Dibaca oleh Nano Kernel saat booting.
;
; GRAMMAR KERNEL (kernel/include/bsp_format.h):
;   magic 8-byte "BSP164\0\0"   (BSP164 = CISC 64-bit)
;   header 24 B: magic(8) ns(2) ver(1) flags(1) entry(4) bsid(4) persona(4)
;   tabel seksi 16 B/entri { type u32, off u32, size u32, vaddr u32 }
;   seksi: .text .rodata .data .bss .cherryreva .icon0 .icon1 .hook
;          .manifest
;
; Layout:
;   [Header 24 B] [Section Table] [.text] [.rodata] [.data] [.bss]
;                 [.cherryreva] [.hook]
;
; ═══════════════════════════════════════════════════════════════════════════

; ── Magic (8-byte LE): 'B','S','P','1','6','4',0,0 ──
BSP_MAGIC_0 equ 'B'
BSP_MAGIC_1 equ 'S'
BSP_MAGIC_2 equ 'P'
BSP_MAGIC_3 equ '1'   ; ISA digit: '1' = CISC (x86)
BSP_MAGIC_4 equ '6'
BSP_MAGIC_5 equ '4'
BSP_MAGIC_6 equ 0
BSP_MAGIC_7 equ 0

; ── Header BSP — 24 B (identik bb_bsp_header kernel) ──
;   magic(8) + num_sections u16(2) + version u8(1) + flags u8(1)
;   + entry_point u32(4) + bsid u32(4) + persona u32(4)
macro format_BSP bsid=0, persona=0 {
    format binary
    db BSP_MAGIC_0, BSP_MAGIC_1, BSP_MAGIC_2, BSP_MAGIC_3
    db BSP_MAGIC_4, BSP_MAGIC_5, BSP_MAGIC_6, BSP_MAGIC_7
    dw 0                        ; num_sections (diisi belink/assembler)
    db 1                        ; version
    db 1                        ; flags: bit0 = hot-swappable
    dd 0                        ; entry_point (0 = pakai .hook)
    dd bsid                     ; BSID: identity scheduler (0..63)
    dd persona                  ; persona bitmask
}

; ── BSP Section Types (identik kernel) ──
BSP_SECT_TEXT       equ 0
BSP_SECT_RODATA     equ 1
BSP_SECT_DATA       equ 2
BSP_SECT_BSS        equ 3
BSP_SECT_CHERRYREVA equ 4
BSP_SECT_ICON0      equ 5
BSP_SECT_ICON1      equ 6
BSP_SECT_HOOK       equ 7
BSP_SECT_MANIFEST   equ 8

; Scheduler Policies (identik kernel bb_bsp_policy)
BSP_POLICY_MLFQ       equ 1
BSP_POLICY_RR         equ 2
BSP_POLICY_EDF        equ 3
BSP_POLICY_PREMPTIVE  equ 4
BSP_POLICY_COOPERATIVE equ 5
BSP_POLICY_LIFESAVER  equ 6
BSP_POLICY_FEMP       equ 7
BSP_POLICY_FEMP_MP    equ 8
