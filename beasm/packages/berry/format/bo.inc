; ═══════════════════════════════════════════════════════════════════════════
; bo.inc — Berry Object (.bo) — Intermediate Object Format
; ═══════════════════════════════════════════════════════════════════════════
;
; File setengah matang (seperti .o / .obj di GCC). Berisi instruksi mesin
; namun alamat memorinya belum direlokasi (masih 0x0000). Harus digabungkan
; (linked) dengan .bo lain oleh belink untuk menjadi .befb / .bdl.
;
; Layout:
;   [Header "BOBJ"] [Section Table] [Symbol Table]
;   [Relocation Table] [.text data] [.rodata data] [.data data]
;
; ═══════════════════════════════════════════════════════════════════════════

macro format_BO {
    format binary
    db 'B','O','B','J'         ; magic "BOBJ"
    dw 1                        ; version
    dw 0                        ; num_sections (patched by beasm)
    dd 0                        ; num_symbols
    dd 0                        ; num_relocs
}

; ── Symbol Types ──
BO_SYM_FUNC    equ 0   ; Function (code)
BO_SYM_DATA    equ 1   ; Data (read/write)
BO_SYM_EXTERN  equ 2   ; External reference (unresolved)
BO_SYM_ABS     equ 3   ; Absolute address

; ── Relocation Types ──
BO_RELOC_ABS   equ 0   ; Absolute (patch full address)
BO_RELOC_REL   equ 1   ; Relative (patch offset from current)
BO_RELOC_GOT   equ 2   ; GOT entry
BO_RELOC_PLT   equ 3   ; PLT entry

; ── Symbol Table Entry (16 bytes) ──
; dd name_hash       ; FNV-1a of symbol name
; dd section_index   ; Which section (0=abs, 0xFFFF=extern)
; dd offset          ; Offset within section
; db type            ; BO_SYM_*
; db binding         ; 0=local, 1=global, 2=weak
; dw reserved

; ── Relocation Entry (12 bytes) ──
; dd offset          ; Where to patch (in section)
; dd symbol_index    ; Which symbol
; db type            ; BO_RELOC_*
; db width           ; 4 or 8 bytes
; dw addend          ; Additional offset

; ═══════════════════════════════════════════════════════════════════════════
; Object Macros
; ═══════════════════════════════════════════════════════════════════════════

macro bo_section_text {
    section '.text' code
}

macro bo_section_data {
    section '.data' data
}

macro bo_section_bss {
    section '.bss' bss
}

; Declare an external symbol reference
macro bo_extern name {
    local hash_val
    fnv1a_32 hash_val, `name
    ; Emit external symbol reference (resolved by belink)
    dd hash_val
    dd 0xFFFFFFFF    ; section = extern
    dd 0             ; offset
    db BO_SYM_EXTERN
    db 1             ; global binding
    dw 0
}
