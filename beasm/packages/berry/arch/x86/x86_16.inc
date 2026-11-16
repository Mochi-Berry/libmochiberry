; ═══════════════════════════════════════════════════════════════════════════
; x86_16.inc — x86 16-bit Real Mode (8086 / 80186 / 80286)
; ═══════════════════════════════════════════════════════════════════════════
;
; Digunakan untuk bootloader MBR, BIOS calls, dan transisi ke Protected Mode.
; Saat CPU menyala (BIOS Legacy), prosesor dalam mode ini — "amnesia" 16-bit
; dari tahun 1978 (Intel 8086) sebelum pindah ke mode modern.
;
; Register: AX/BX/CX/DX/SI/DI/BP/SP (16-bit)
; Addressing: Segment:Offset (CS:IP, DS:SI, ES:DI) — 1 MB via A20
; Prefix 0x66 otomatis untuk operasi 32-bit di mode 16-bit.
;
; ═══════════════════════════════════════════════════════════════════════════

use16

; ── Register Aliases (UBerry ABI) ──
r0 equ ax
r1 equ bx
r2 equ cx
r3 equ dx
r4 equ si
r5 equ di
r6 equ bp
r7 equ sp

; ── Segment Registers ──
seg_cs equ cs
seg_ds equ ds
seg_es equ es
seg_ss equ ss
seg_fs equ fs
seg_gs equ gs

; ── Flag Bits (eflags) ──
FLAG_CF equ (1 shl 0)   ; Carry
FLAG_PF equ (1 shl 2)   ; Parity
FLAG_AF equ (1 shl 4)   ; Auxiliary Carry
FLAG_ZF equ (1 shl 6)   ; Zero
FLAG_SF equ (1 shl 7)   ; Sign
FLAG_TF equ (1 shl 8)   ; Trap
FLAG_IF equ (1 shl 9)   ; Interrupt Enable
FLAG_DF equ (1 shl 10)  ; Direction
FLAG_OF equ (1 shl 11)  ; Overflow
FLAG_NT equ (1 shl 14)  ; Nested Task

; ── Memory Map Real Mode ──
RM_IVT      equ 0x0000   ; Interrupt Vector Table (1 KB)
RM_BDA      equ 0x0400   ; BIOS Data Area (256 B)
RM_EBDA     equ 0x9FC00  ; Extended BIOS Data Area
RM_VGA_TEXT equ 0xB8000  ; VGA text buffer (80x25)
RM_VGA_GFX  equ 0xA0000  ; VGA graphics (mode 13h)
RM_BIOS     equ 0xF0000  ; BIOS ROM

; ── Standard Memory Layouts ──
RM_LOWMEM_MAX equ 0x90000  ; conventional memory end (576 KB)

; ── Ports ──
PORT_PIT_CMD    equ 0x43   ; PIT command
PORT_PIT_CH0    equ 0x40   ; PIT channel 0 (tick)
PORT_PIT_CH1    equ 0x41   ; PIT channel 1 (refresh)
PORT_PIT_CH2    equ 0x42   ; PIT channel 2 (speaker)
PORT_PPI_B      equ 0x61   ; PPI port B (speaker gate)
PORT_KBD_DATA   equ 0x60   ; keyboard data
PORT_KBD_CMD    equ 0x64   ; keyboard command
PORT_CMOS_IDX   equ 0x70   ; CMOS index
PORT_CMOS_DATA  equ 0x71   ; CMOS data
PORT_A20_FAST   equ 0x92   ; Fast A20 gate

; ── BIOS Interrupt Numbers ──
INT_BIOS_VIDEO      equ 0x10   ; video services
INT_BIOS_DISK       equ 0x13   ; disk services
INT_BIOS_KBD        equ 0x16   ; keyboard services
INT_BIOS_MEM        equ 0x15   ; memory services (E820)
INT_BIOS_TIMER      equ 0x1A   ; time services
INT_BIOS_SYS        equ 0x21   ; DOS system calls

; ── BIOS Video Services (int 0x10) ──
VIDEO_SET_MODE      equ 0x00   ; AH=00, AL=mode
VIDEO_SET_CURSOR    equ 0x02   ; AH=02, DH=row, DL=col
VIDEO_TELETYPE      equ 0x0E   ; AH=0E, AL=char (teletype output)
VIDEO_GET_MODE      equ 0x0F   ; AH=0F → AL=mode
VIDEO_READ_CHAR     equ 0x08   ; AH=08, BH=page → AL=char
VIDEO_SET_PAGE      equ 0x05   ; AH=05, AL=page
VIDEO_STRING        equ 0x13   ; AH=13, ES:BP=string

; ── BIOS Disk Services (int 0x13) ──
DISK_RESET      equ 0x00   ; AH=00
DISK_READ       equ 0x02   ; AH=02, CHS read
DISK_WRITE      equ 0x03   ; AH=03, CHS write
DISK_VERIFY     equ 0x04   ; AH=04
DISK_GET_PARAM  equ 0x08   ; AH=08 → CH=cyl, DH=head
DISK_EXT_READ   equ 0x42   ; AH=42, LBA read (EDD)
DISK_EXT_WRITE  equ 0x43   ; AH=43, LBA write
DISK_EXT_CHECK  equ 0x41   ; AH=41, EDD check

; ── BIOS Memory Services (int 0x15) ──
MEM_E820        equ 0xE820  ; AX=E820: memory map (ES:DI = buffer)
MEM_CONVENTIONAL equ 0x88  ; AH=88: extended memory size
MEM_E801        equ 0xE801  ; AX=E801: memory size

; ── VGA Text Modes ──
MODE_TEXT_40x25  equ 0x00
MODE_TEXT_80x25  equ 0x03
MODE_TEXT_80x50  equ 0x12
MODE_GFX_320x200 equ 0x13  ; 256-color VGA

; ── VGA Colors (text mode attribute) ──
COLOR_BLACK      equ 0x0
COLOR_BLUE       equ 0x1
COLOR_GREEN      equ 0x2
COLOR_CYAN       equ 0x3
COLOR_RED        equ 0x4
COLOR_MAGENTA    equ 0x5
COLOR_BROWN      equ 0x6
COLOR_LIGHT_GRAY equ 0x7
COLOR_DARK_GRAY  equ 0x8
COLOR_LIGHT_BLUE equ 0x9
COLOR_LIGHT_GREEN equ 0xA
COLOR_LIGHT_CYAN equ 0xB
COLOR_LIGHT_RED  equ 0xC
COLOR_LIGHT_MAGENTA equ 0xD
COLOR_YELLOW     equ 0xE
COLOR_WHITE      equ 0xF

; ── Attribute helper: foreground | (background shl 4) | blink bit ──
ATTR_BLINK equ (1 shl 7)

; ═══════════════════════════════════════════════════════════════════════════
; Instruction Macros
; ═══════════════════════════════════════════════════════════════════════════

; ── BIOS print char (teletype) ──
macro bios_print_char char {
    mov ah, 0x0E
    mov al, char
    int 0x10
}

; ── BIOS print string (ES:BP, CX=len) ──
macro bios_print_string str_ptr, str_len {
    mov ah, 0x13
    mov al, 1            ; write string + update cursor
    mov bh, 0            ; page 0
    mov bl, 0x07         ; white on black
    mov cx, str_len
    push ds
    pop es
    mov bp, str_ptr
    int 0x10
}

; ── BIOS read disk (CHS) ──
; drive=A:0/B:1/C:0x80, cyl/head/sector, num_sects, buf_seg:buf_off
macro bios_read_disk drive, cyl, head, sector, num_sects, buf_seg, buf_off {
    mov ah, 0x02
    mov al, num_sects
    mov ch, cyl
    mov cl, sector
    mov dh, head
    mov dl, drive
    mov bx, buf_off
    mov es, buf_seg
    int 0x13
    ; CF=1 on error, AH=error code, AL=sectors read
}

; ── BIOS read disk (LBA/EDD) ──
; drive, dap_ptr (disk address packet @ DS:SI)
macro bios_read_disk_lba drive, dap_ptr {
    mov ah, 0x42
    mov dl, drive
    mov si, dap_ptr
    int 0x13
}

; ── BIOS get memory map (E820) ──
; buf_ptr (ES:DI), entry_size (24), max_entries
; Setelah call: CF=1 done; EBX=0 untuk iterasi terakhir
macro bios_e820 buf_ptr, entry_size, max_entries {
    mov ax, 0xE820
    mov cx, entry_size
    mov edx, 0x534D4150      ; "SMAP"
    mov di, buf_ptr
    xor ebx, ebx             ; start dari entry 0
    int 0x15
}

; ── Set VGA mode ──
macro bios_set_mode mode {
    mov ax, mode
    int 0x10
}

; ── Set text cursor ──
macro bios_set_cursor row, col {
    mov ah, 0x02
    mov bh, 0           ; page 0
    mov dh, row
    mov dl, col
    int 0x10
}

; ═══════════════════════════════════════════════════════════════════════════
; Protected Mode Transition
; ═══════════════════════════════════════════════════════════════════════════

; ── Fast A20 gate (port 0x92) ──
macro a20_enable_fast {
    in al, 0x92
    or al, 2
    and al, 0xFE        ; bit0 = reset (jangan set)
    out 0x92, al
}

; ── A20 via keyboard controller (legacy) ──
macro a20_enable_kbd {
    ; wait input buffer empty
    call $+3
    a20_wait_in:
    in al, 0x64
    test al, 2
    jnz a20_wait_in
    mov al, 0xD1        ; write to output port
    out 0x64, al
    a20_wait_in2:
    in al, 0x64
    test al, 2
    jnz a20_wait_in2
    mov al, 0xDF        ; enable A20
    out 0x60, al
    a20_wait_in3:
    in al, 0x64
    test al, 2
    jnz a20_wait_in3
}

; ── Load GDT ──
macro gdt_load gdt_ptr {
    lgdt [gdt_ptr]
}

; ── Enter Protected Mode ──
; Panggil setelah GDT siap: jmp 0x08:protected_entry
macro enter_protected_mode {
    mov eax, cr0
    or  eax, 1          ; PE bit
    mov cr0, eax
    jmp 0x08:protected_entry
}

; ═══════════════════════════════════════════════════════════════════════════
; 16-bit I/O Helpers
; ═══════════════════════════════════════════════════════════════════════════

macro io_out8 port, val {
    mov al, val
    mov dx, port
    out dx, al
}

macro io_in8 port, dest {
    mov dx, port
    in al, dx
    mov dest, al
}
