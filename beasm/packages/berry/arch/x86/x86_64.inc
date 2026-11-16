; ═══════════════════════════════════════════════════════════════════════════
; x86_64.inc — x86-64 Long Mode (AMD64 / Intel 64)
; ═══════════════════════════════════════════════════════════════════════════
;
; Register: RAX-R15 (64-bit), XMM0-XMM15 (SSE), RIP-relative addressing
; Addressing: 48-bit virtual canonical (0x0000..0x7FFF + 0xFFFF8000..)
; Syscall: syscall/sysret (fast path — UBerry convention)
; Calling: System V AMD64 ABI (RDI, RSI, RDX, RCX, R8, R9, stack)
; REX prefix (0x40-0x4F) untuk register 64-bit & R8-R15.
;
; ═══════════════════════════════════════════════════════════════════════════

use64

; ── Register Aliases (System V AMD64 ABI) ──
r0  equ rdi       ; Arg 1 / return
r1  equ rsi       ; Arg 2
r2  equ rdx       ; Arg 3
r3  equ rcx       ; Arg 4
r4  equ r8        ; Arg 5
r5  equ r9        ; Arg 6
r6  equ rbp       ; Frame pointer
r7  equ rsp       ; Stack pointer
r16 equ rbx       ; Callee-saved
r17 equ r12
r18 equ r13
r19 equ r14
r20 equ r15
r21 equ r10       ; Syscall clobbered
r22 equ r11       ; Syscall clobbered (flags)

; ── Return value alias ──
retval equ rax

; ── REX Prefix Bytes ──
REX_W    equ 0x48   ; 64-bit operand size
REX_R    equ 0x44   ; ModRM.reg extends (R8-R15)
REX_X    equ 0x42   ; SIB.index extends
REX_B    equ 0x41   ; ModRM.rm / SIB.base extends
REX_WR   equ 0x4C   ; W+R
REX_WB   equ 0x49   ; W+B
REX_WRB  equ 0x4D   ; W+R+B
REX_RXB  equ 0x47   ; R+X+B

; ── GDT (Long Mode — 64-bit code segment) ──
GDT_NULL      equ 0x00
GDT_CODE64    equ 0x08   ; ring-0 64-bit code (L bit set)
GDT_DATA64    equ 0x10   ; ring-0 data
GDT_CODE64_R3 equ 0x18   ; ring-3 64-bit code
GDT_DATA64_R3 equ 0x20   ; ring-3 data
GDT_TSS       equ 0x28   ; TSS (I/O map base)

; ── CR0 Flags ──
CR0_PE  equ 1
CR0_MP  equ (1 shl 1)
CR0_EM  equ (1 shl 2)
CR0_TS  equ (1 shl 3)
CR0_ET  equ (1 shl 4)
CR0_NE  equ (1 shl 5)
CR0_WP  equ (1 shl 16)
CR0_AM  equ (1 shl 18)
CR0_NW  equ (1 shl 29)
CR0_CD  equ (1 shl 30)
CR0_PG  equ (1 shl 31)

; ── CR4 Flags ──
CR4_VME    equ (1 shl 0)
CR4_PVI    equ (1 shl 1)
CR4_TSD    equ (1 shl 2)
CR4_DE     equ (1 shl 3)
CR4_PSE    equ (1 shl 4)
CR4_PAE    equ (1 shl 5)   ; REQUIRED for long mode
CR4_MCE    equ (1 shl 6)
CR4_PGE    equ (1 shl 7)
CR4_PCE    equ (1 shl 8)
CR4_OSFXSR equ (1 shl 9)
CR4_OSXMMEXCPT equ (1 shl 10)
CR4_UMIP   equ (1 shl 11)
CR4_OSXSAVE equ (1 shl 18)
CR4_SMEP   equ (1 shl 20)
CR4_SMAP   equ (1 shl 21)

; ── MSR Addresses ──
MSR_EFER       equ 0xC0000080
MSR_STAR       equ 0xC0000081   ; SYSCALL/SYSRET segment bases
MSR_LSTAR      equ 0xC0000082   ; SYSCALL RIP
MSR_FMASK      equ 0xC0000084   ; SYSCALL flags mask
MSR_FS_BASE    equ 0xC0000100
MSR_GS_BASE    equ 0xC0000101
MSR_KERNEL_GS  equ 0xC0000102   ; swapgs target
MSR_TSC        equ 0x10
MSR_APIC_BASE  equ 0x1B
MSR_MTRR_BASE  equ 0x2FF

; ── EFER Bits ──
EFER_SCE  equ 1                    ; SYSCALL/SYSRET enable
EFER_LME  equ (1 shl 8)            ; Long Mode Enable
EFER_LMA  equ (1 shl 10)           ; Long Mode Active (read-only)
EFER_NXE  equ (1 shl 11)           ; No-Execute Enable
EFER_SVME equ (1 shl 12)           ; SVM Enable (AMD)

; ── Page Table Entry Flags (4-level paging) ──
PAGE_PRESENT   equ 0x0001
PAGE_WRITABLE  equ 0x0002
PAGE_USER      equ 0x0004
PAGE_WRTHROUGH equ 0x0008
PAGE_CACHE_DIS equ 0x0010
PAGE_ACCESSED  equ 0x0020
PAGE_DIRTY     equ 0x0040
PAGE_2MB       equ 0x0080   ; PS bit (PDE)
PAGE_1GB       equ 0x0080   ; PS bit (PDPTE)
PAGE_GLOBAL    equ 0x0100
PAGE_PAT       equ 0x0400   ; Page Attribute Table
PAGE_NX        equ (1 shl 63)  ; No-Execute (if EFER.NXE)

; ── Virtual Memory Layout (kernel) ──
VM_KERNEL_BASE  equ 0xFFFFFFFF80000000   ; -2 GB (canonical)
VM_KERNEL_END   equ 0xFFFFFFFFFFFFFFFF
VM_USER_TOP     equ 0x00007FFFFFFFFFFF   ; user space ceiling

; ── Canonical address helpers ──
; 48-bit sign-extended: 0..0x7FFFFFFFFFFF (user), 0xFFFF800000000000.. (kernel)

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

; ── MSR Access ──
macro rdmsr_64 {
    rdmsr          ; ECX=addr → EDX:EAX
}

macro wrmsr_64 {
    wrmsr          ; ECX=addr, EDX:EAX=value
}

; ── Fast syscall setup (MSR LSTAR) ──
; entry = handler address, cs_sel = code selector, ss_sel = stack selector
macro setup_syscall entry, cs_sel, ss_sel {
    ; EFER.SCE
    mov ecx, MSR_EFER
    rdmsr
    or eax, EFER_SCE
    wrmsr
    ; STAR = SS<<48 | CS<<32 (CS for syscall, SS for sysret)
    mov ecx, MSR_STAR
    mov edx, (cs_sel shl 8) shr 16
    mov eax, ((cs_sel + 8) shl 16) or cs_sel
    wrmsr
    ; LSTAR = entry
    mov ecx, MSR_LSTAR
    mov eax, entry and 0xFFFFFFFF
    mov edx, (entry shr 32) and 0xFFFFFFFF
    wrmsr
    ; FMASK = clear IF, DF (bit 9, 10)
    mov ecx, MSR_FMASK
    xor eax, eax
    mov edx, eax
    bts eax, 9
    bts eax, 10
    wrmsr
}

; ── CR3 reload / TLB flush ──
macro tlb_flush_all {
    mov rax, cr3
    mov cr3, rax
}

; ── Pause (spinlock hint) ──
macro pause_hint {
    pause
}

; ── Memory barriers ──
macro mfence_64 {
    mfence
}
macro sfence_64 {
    sfence
}
macro lfence_64 {
    lfence
}

; ── Segment reload (long mode) ──
macro reload_segments_64 {
    mov ax, GDT_DATA64
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
}

; ── Swapgs (enter/exit kernel GS) ──
macro swapgs_64 {
    swapgs
}

; ── CPUID helpers ──
macro cpuid_longmode_check {
    mov eax, 0x80000001
    cpuid            ; bit29 EDX = LM (long mode supported)
}

; ═══════════════════════════════════════════════════════════════════════════
; Instruksi Sistem Ring-0 — didukung NATIVE oleh beasm.c (x86-64)
; ═══════════════════════════════════════════════════════════════════════════
;   I/O port   : in/out (al/ax/eax ↔ dx/imm8)
;   MSR/TSC    : rdmsr, wrmsr, rdpmc, rdtsc, rdtscp, cpuid, xgetbv, xsetbv
;   SMAP       : clac, stac
;   Control    : mov cr0..cr15, r64 | mov r64, cr0..cr15
;   Debug      : mov dr0..dr15, r64 | mov r64, dr0..dr15
;   Descriptor : lgdt, lidt, sgdt, sidt, lldt, ltr, sldt, str, invlpg
;   Cache      : wbinvd, invd, clflush, clflushopt, prefetcht0/t1/t2/nta
;   Misc       : swapgs, iretq, syscall/sysret/sysenter/sysexit, pause,
;                mfence/lfence/sfence, endbr64, lahf/sahf, clc/stc/cmc,
;                xlatb, ud2, rdrand, rdseed
;   String ops : rep/repe/repz/repne/repnz + movs/stos/lods/cmps/scas/
;                ins/outs (b/w/d/q)
;   Atomic     : lock + cmpxchg, cmpxchg8b/16b, xadd, xchg
;   Bit        : bt/bts/btr/btc, bsf/bsr, bswap
;   Shift      : shld/shrd (imm8|cl)
;   Loop       : loop/loope/loopne (rel8), jrcxz/jecxz
;
; (Jumlah total set instruksi x86-64: ~981 mnemonic / ~3.6 rb varian
;  kumulatif dari 8086 — beasm mencakup subset inti ring-0 + kernel work.)
; ═══════════════════════════════════════════════════════════════════════════

; ── Control register move helpers ──
macro set_cr0 val {
    mov rax, val
    mov cr0, rax
}

macro set_cr3 val {
    mov rax, val
    mov cr3, rax
}

macro set_cr4 val {
    mov rax, val
    mov cr4, rax
}

; ── Descriptor table reload ──
macro reload_gdt ptr {
    lgdt [ptr]
    mov ax, GDT_DATA64
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
}

; ── Serialize (ensure prior instructions retire) ──
macro serialize_cpu {
    xor eax, eax
    cpuid
}

; ── Spinlock (xchg-based) ──
macro spinlock_try lock_ptr {
    xor eax, eax
    inc eax           ; want = 1
    xchg [lock_ptr], eax
    ; eax = 0 → acquired, 1 → held
}

; ── Read canonical MSR into rax ──
; ecx = msr addr; hasil rax (64-bit penuh)
macro read_msr rax_out {
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov rax_out, rax
}

; ── Write canonical MSR from rax ──
; ecx = msr addr; rax = value 64-bit
macro write_msr rax_val {
    mov rax_val, rax
    mov rdx, rax
    shr rdx, 32
    wrmsr
}
