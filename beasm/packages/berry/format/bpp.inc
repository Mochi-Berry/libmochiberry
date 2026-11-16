; ═══════════════════════════════════════════════════════════════════════════
; bpp.inc — Berry Panel Program (.bpp) — GUI APP MILIK DE/WM/DS
; ═══════════════════════════════════════════════════════════════════════════
;
; GRAMMAR KERNEL (kernel/include/bpp_format.h):
;   magic 8-byte "BPP164\0\0"
;   header 24 B: magic(8) ns(2) ver(1) flags(1) entry(4) win_w(4) win_h(4)
;   tabel seksi 16 B/entri { type u32, off u32, size u32, vaddr u32 }
;   seksi: .text .rodata .data .bss .cherryreva .hook .manifest
;          (exports TIDAK dipakai — .bpp berkomunikasi lewat .hook API table)
;
; .bpp = aplikasi GUI STANDALONE yang TIDAK dikompilasi ke nullde.befb:
; DE menemukannya di /system/app/, memuat via LBRT (BERRYCALL_BPP_LOAD
; 0x142, bpp_loader.c), CherryReva-relokasi, lalu mem-dispatch event
; create/draw/key/mouse/close lewat tabel .hook (bb_bpp_api_t).
;
; Satu-satunya tautan app ke DE/WM/DS = SERVICE TABLE yang disuntik saat
; bind (bb_bpp_services_t) — pointer fungsi polos (fill/frame/text/strlen/
; hfill/resize/call), ZERO syscall.  Itu "lib DE/WM/DS"-nya.
;
; ╔══════════════════════════════════════════════════════════════════════════╗
; ║  HIERARKI:  .bex = nano kernel · .befb = LBRT · .bpp = DE/WM/DS        ║
; ║  (.bdl = komponen pasif · .lbrct/.hbrct = bundel runtime)              ║
; ╚══════════════════════════════════════════════════════════════════════════╝
;

; ── Magic (8-byte LE): 'B','P','P','1','6','4',0,0 ──
BPP_MAGIC_0 equ 'B'
BPP_MAGIC_1 equ 'P'
BPP_MAGIC_2 equ 'P'
BPP_MAGIC_3 equ '1'
BPP_MAGIC_4 equ '6'
BPP_MAGIC_5 equ '4'
BPP_MAGIC_6 equ 0
BPP_MAGIC_7 equ 0

; ── Header BPP — 24 B (identik bb_bpp_header kernel) ──
;   magic(8) + num_sections u16(2) + version u8(1) + flags u8(1)
;   + entry u32(4) + win_w u32(4) + win_h u32(4)
macro format_BPP {
    format binary
    db BPP_MAGIC_0, BPP_MAGIC_1, BPP_MAGIC_2, BPP_MAGIC_3
    db BPP_MAGIC_4, BPP_MAGIC_5, BPP_MAGIC_6, BPP_MAGIC_7
    dw 0                        ; num_sections (diisi belink/assembler)
    db 1                        ; version (format version 1)
    db 0                        ; flags: bit0 = single-instance app
    dd 0                        ; entry_point (0 = pakai .hook API table)
    dd 0                        ; win_w (0 = DE memutuskan)
    dd 0                        ; win_h (0 = DE memutuskan)
}

; ── BPP Section Types (identik kernel bb_bpp_sect_type) ──
BPP_SECT_TEXT       equ 0
BPP_SECT_RODATA     equ 1
BPP_SECT_DATA       equ 2
BPP_SECT_BSS        equ 3
BPP_SECT_CHERRYREVA equ 4
BPP_SECT_HOOK       equ 7
BPP_SECT_MANIFEST   equ 8

macro bpp_section_text {
    section '.text' code executable
}
macro bpp_section_rodata {
    section '.rodata' data readable
}
macro bpp_section_data {
    section '.data' data readable writable
}
macro bpp_section_bss {
    section '.bss' data readable writable
}
macro bpp_section_hook {
    section '.hook' data readable
}
macro bpp_section_manifest {
    section '.manifest' data readable
}

; ── CherryReva RVA entry (8 B, identik semua format) ──
;   offset u32 + type u8 (0 = absolute) + width u8 (4/8) + rsv u16
BPP_CR_OFFSET  equ 0
BPP_CR_TYPE    equ 4
BPP_CR_WIDTH   equ 5
BPP_CR_RSV     equ 6
BPP_CR_ENTRY_SIZE equ 8

; ── Manifest TLV tags (identik bb_bpp_manifest_tag kernel) ──
BPP_TAG_APP_NAME     equ 0x0001
BPP_TAG_VERSION      equ 0x0002
BPP_TAG_CAPABILITIES equ 0x0003
BPP_TAG_CREATOR      equ 0x0004
BPP_TAG_DESCRIPTION  equ 0x0005
BPP_TAG_ICON0_DIM    equ 0x0006
BPP_TAG_ABI_VERSION  equ 0x0013
BPP_TAG_TITLE        equ 0x0020   ; judul jendela default
BPP_TAG_TERMINATOR   equ 0xFFFF

BPP_ABI_VERSION equ 1

; ═══════════════════════════════════════════════════════════════════════════
; API TABLE .hook — bb_bpp_api_t (natural alignment, C3)
; ═══════════════════════════════════════════════════════════════════════════
; Layout (pointer 64-bit = 8 B tiap slot):
;   +0  abi_version u32  +4 flags u32
;   +8  bind(Ctx*)      +16 create(Ctx*)
;   +24 draw(Ctx*)      +32 key(Ctx*, keysym, ascii)
;   +40 mouse(Ctx*,x,y,btns)  +48 close(Ctx*)
;   +56 info(char*, u32)
BPP_API_ABI_VER equ 0
BPP_API_FLAGS   equ 4
BPP_API_BIND    equ 8
BPP_API_CREATE  equ 16
BPP_API_DRAW    equ 24
BPP_API_KEY     equ 32
BPP_API_MOUSE   equ 40
BPP_API_CLOSE   equ 48
BPP_API_INFO    equ 56
BPP_API_SIZE    equ 64

; ═══════════════════════════════════════════════════════════════════════════
; SERVICE TABLE (disuntik DE saat bind) — bb_bpp_services_t
; ═══════════════════════════════════════════════════════════════════════════
;   +0  abi_version u32  +4 reserved u32
;   +8  fill(Ctx*,x0,y0,x1,y1,color)
;   +16 frame(Ctx*,x0,y0,x1,y1,color)
;   +24 text(Ctx*,x,y,str,color,scale)
;   +32 strlen(Ctx*,str)
;   +40 hfill(Ctx*,x,y,w,h,color)
;   +48 resize(Ctx*,w,h)
;   +56 call(Ctx*,cmd,a,b)
BPP_SVC_ABI_VER equ 0
BPP_SVC_RESERVED equ 4
BPP_SVC_FILL    equ 8
BPP_SVC_FRAME   equ 16
BPP_SVC_TEXT    equ 24
BPP_SVC_STRLEN  equ 32
BPP_SVC_HFILL   equ 40
BPP_SVC_RESIZE  equ 48
BPP_SVC_CALL    equ 56
BPP_SVC_SIZE    equ 64

; ── svc->call() commands (identik BPP_CALL_* kernel) ──
BPP_CALL_NOP            equ 0
BPP_CALL_PIC_INFO       equ 1
BPP_CALL_PIC_DECODE     equ 2
BPP_CALL_PIC_DEC_ALLOC  equ 3
BPP_CALL_PIC_FREE_ALLOC equ 4
BPP_CALL_FS_READ_ALLOC  equ 7
BPP_CALL_FS_FREE_ALLOC  equ 8
BPP_CALL_REDRAW         equ 9
BPP_CALL_MEM_ALLOC      equ 10
BPP_CALL_MEM_FREE       equ 11

; ═══════════════════════════════════════════════════════════════════════════
; CONTEXT .bpp — bb_bpp_ctx_t (natural alignment)
; ═══════════════════════════════════════════════════════════════════════════
;   +0  win_id u32  +4 flags u32
;   +8  x,y,w,h (int×4)
;   +24 cx,cy (int×2)  +32 mouse_btn u32  +36 keysym u32  +40 ascii char
;   +48 fb ptr  +56 fb_w int  +60 fb_h int  +64 time_ms u32
;   +72 user ptr  +80 svc ptr
BPP_CTX_WIN_ID   equ 0
BPP_CTX_FLAGS    equ 4
BPP_CTX_X        equ 8
BPP_CTX_Y        equ 12
BPP_CTX_W        equ 16
BPP_CTX_H        equ 20
BPP_CTX_CX       equ 24
BPP_CTX_CY       equ 28
BPP_CTX_MOUSE_BTN equ 32
BPP_CTX_KEYSYM   equ 36
BPP_CTX_ASCII    equ 40
BPP_CTX_FB       equ 48
BPP_CTX_FB_W     equ 56
BPP_CTX_FB_H     equ 60
BPP_CTX_TIME_MS  equ 64
BPP_CTX_USER     equ 72
BPP_CTX_SVC      equ 80
BPP_CTX_SIZE     equ 88
