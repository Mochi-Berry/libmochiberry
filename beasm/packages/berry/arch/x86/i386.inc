; ═══════════════════════════════════════════════════════════════════════════
; i386.inc — x86 32-bit Protected Mode (i386 / i486 / i586 / i686)
; ═══════════════════════════════════════════════════════════════════════════
;
; Keluarga x86 32-bit: i386, i486, i586 (Pentium), i686 (Pentium Pro/MMX).
; Semua berbagi satu ISA 32-bit — perbedaannya di instruksi tambahan
; (i486: CMPXCHG/XADD/BXCHG8B; i586: MMX; i686: CMOV/FCOMI).
;
; Register: EAX/EBX/ECX/EDX/ESI/EDI/EBP/ESP (32-bit)
; Addressing: Flat memory model (4 GB), segment base 0
; Syscall: int 0x80 (UBerry convention)
;
; ═══════════════════════════════════════════════════════════════════════════

use32

; ── Register Aliases ──
r0 equ eax
r1 equ ebx
r2 equ ecx
r3 equ edx
r4 equ esi
r5 equ edi
r6 equ ebp
r7 equ esp

; ── Segment Selectors ──
GDT_NULL      equ 0x00   ; null descriptor
GDT_CODE32    equ 0x08   ; ring-0 code
GDT_DATA32    equ 0x10   ; ring-0 data
GDT_STACK32   equ 0x18   ; ring-0 stack
GDT_CODE3     equ 0x20   ; ring-3 code
GDT_DATA3     equ 0x28   ; ring-3 data
GDT_TSS       equ 0x30   ; task state segment

; ── CR0 Flags ──
CR0_PE  equ 1                    ; Protection Enable
CR0_MP  equ (1 shl 1)            ; Monitor Coprocessor
CR0_EM  equ (1 shl 2)            ; Emulation (FPU)
CR0_TS  equ (1 shl 3)            ; Task Switched
CR0_ET  equ (1 shl 4)            ; Extension Type
CR0_NE  equ (1 shl 5)            ; Numeric Error
CR0_WP  equ (1 shl 16)           ; Write Protect (supervisor)
CR0_AM  equ (1 shl 18)           ; Alignment Mask
CR0_NW  equ (1 shl 29)           ; Not Write-through
CR0_CD  equ (1 shl 30)           ; Cache Disable
CR0_PG  equ (1 shl 31)           ; Paging

; ── CR4 Flags ──
CR4_VME    equ (1 shl 0)         ; Virtual 8086 Mode Extensions
CR4_PVI    equ (1 shl 1)         ; Protected-mode Virtual Interrupts
CR4_TSD    equ (1 shl 2)         ; Time Stamp Disable
CR4_DE     equ (1 shl 3)         ; Debugging Extensions
CR4_PSE    equ (1 shl 4)         ; Page Size Extensions (4 MB pages)
CR4_PAE    equ (1 shl 5)         ; Physical Address Extensions
CR4_MCE    equ (1 shl 6)         ; Machine Check Enable
CR4_PGE    equ (1 shl 7)         ; Page Global Enable
CR4_OSFXSR equ (1 shl 9)         ; OS FXSAVE/FXRSTOR
CR4_OSXMMEXCPT equ (1 shl 10)    ; OS Unmasked Exception Support

; ── Page Table Entry Flags (32-bit paging) ──
PAGE_PRESENT   equ 0x001
PAGE_WRITABLE  equ 0x002
PAGE_USER      equ 0x004
PAGE_WRTHROUGH equ 0x008
PAGE_CACHE_DIS equ 0x010
PAGE_ACCESSED  equ 0x020
PAGE_DIRTY     equ 0x040
PAGE_4MB       equ 0x080   ; PS bit (PDE only)
PAGE_GLOBAL    equ 0x100

; ── Segment Descriptor Access Bytes ──
ACC_CODE_EXEC    equ 0x9A   ; present, ring-0, code, readable
ACC_DATA_RW      equ 0x92   ; present, ring-0, data, writable
ACC_CODE_EXEC_R3 equ 0xFA   ; ring-3 code
ACC_DATA_RW_R3   equ 0xF2   ; ring-3 data
ACC_TSS_AVAIL    equ 0x89   ; available TSS

; ── IDT Gate Types ──
IDT_TASK_GATE    equ 0x85
IDT_INT_GATE_16  equ 0x86
IDT_TRAP_GATE_16 equ 0x87
IDT_INT_GATE_32  equ 0x8E   ; interrupt gate (32-bit, present)
IDT_TRAP_GATE_32 equ 0x8F   ; trap gate
IDT_INT_GATE_32_R3 equ 0xEE ; user-accessible interrupt gate

; ── Port I/O ──
macro outb8 port, val {
    mov al, val
    mov dx, port
    out dx, al
}

macro inb8 port, dest {
    mov dx, port
    in al, dx
    mov dest, al
}

macro outw16 port, val {
    mov ax, val
    mov dx, port
    out dx, ax
}

macro inw16 port, dest {
    mov dx, port
    in ax, dx
    mov dest, ax
}

macro outd32 port, val {
    mov eax, val
    mov dx, port
    out dx, eax
}

macro ind32 port, dest {
    mov dx, port
    in eax, dx
    mov dest, eax
}

; ── IDT/GDT Helpers ──
macro lidt_32 idt_ptr {
    lidt [idt_ptr]
}

macro lgdt_32 gdt_ptr {
    lgdt [gdt_ptr]
}

macro ltr_32 sel {
    ltr sel
}

; ── Segment reload helpers ──
macro reload_segments {
    jmp GDT_CODE32:$+2
    mov ax, GDT_DATA32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
}

; ── Paging setup (identity map) ──
; pd = address of page directory (4 KB aligned)
; pt = address of first page table
macro setup_identity_paging pd, pt {
    ; PDE[0] → pt (present, writable)
    mov eax, pt
    or eax, PAGE_PRESENT or PAGE_WRITABLE
    mov [pd], eax
    ; PT[0..1023] → 0..4 MB (identity, present, writable)
    mov edi, pt
    xor eax, eax
    or eax, PAGE_PRESENT or PAGE_WRITABLE
    mov ecx, 1024
    setup_pt_loop:
    mov [edi], eax
    add edi, 4
    add eax, 0x1000
    loop setup_pt_loop
    ; CR3 = pd
    mov eax, pd
    mov cr3, eax
    ; CR4.PSE + CR0.PG
    mov eax, cr4
    or eax, CR4_PSE
    mov cr4, eax
    mov eax, cr0
    or eax, CR0_PG
    mov cr0, eax
}

; ── CPUID helpers ──
macro cpuid_vendor {
    xor eax, eax
    cpuid          ; EBX:EDX:ECX = "GenuineIntel"/"AuthenticAMD"
}

; ── RDMSR/WRMSR ──
macro rdmsr_32 {
    rdmsr          ; ECX=addr → EDX:EAX
}

macro wrmsr_32 {
    wrmsr          ; ECX=addr, EDX:EAX=value
}
