; ═══════════════════════════════════════════════════════════════════════════
; arm64.inc — ARM 64-bit (AArch64 / ARMv8-A)
; ═══════════════════════════════════════════════════════════════════════════
;
; Register: X0-X30 (64-bit), W0-W30 (32-bit low half of X)
;           SP (stack), LR=X30, PC, PSTATE, ELR_ELx, SPSR_ELx
; Instruksi: 32-bit fixed-length, always-aligned (4 bytes)
; Syscall: SVC #0 (exception level transition)
; Calling: AAPCS64 (X0-X7 args, X0-X1 return, X19-X29 callee-saved)
;
; ═══════════════════════════════════════════════════════════════════════════

use64

; ── General Purpose Registers ──
x0  equ x0    ; Arg 1 / return
x1  equ x1    ; Arg 2 / return
x2  equ x2    ; Arg 3
x3  equ x3    ; Arg 4
x4  equ x4    ; Arg 5
x5  equ x5    ; Arg 6
x6  equ x6    ; Arg 7
x7  equ x7    ; Arg 8
x8  equ x8    ; Indirect result / Syscall ID (SVC)
x9  equ x9    ; Temporary (caller-saved)
x10 equ x10
x11 equ x11
x12 equ x12
x13 equ x13
x14 equ x14
x15 equ x15
x16 equ x16   ; Intra-procedure call (IP0)
x17 equ x17   ; PLT scratch (IP1)
x18 equ x18   ; Platform register
x19 equ x19   ; Callee-saved
x20 equ x20
x21 equ x21
x22 equ x22
x23 equ x23
x24 equ x24
x25 equ x25
x26 equ x26
x27 equ x27
x28 equ x28
x29 equ x29   ; Frame pointer (FP)
x30 equ x30   ; Link register (LR)
sp  equ sp    ; Stack pointer
pc  equ pc    ; Program counter

; ── W-registers (32-bit views) ──
w0  equ w0
w1  equ w1
w2  equ w2
w3  equ w3
w4  equ w4
w5  equ w5
w6  equ w6
w7  equ w7
w8  equ w8
w9  equ w9
w10 equ w10
w11 equ w11
w12 equ w12
w13 equ w13
w14 equ w14
w15 equ w15
w16 equ w16
w17 equ w17
w18 equ w18
w19 equ w19
w20 equ w20
w21 equ w21
w22 equ w22
w23 equ w23
w24 equ w24
w25 equ w25
w26 equ w26
w27 equ w27
w28 equ w28
w29 equ w29
w30 equ w30

; ── Exception Levels ──
EL0 equ 0   ; User (application)
EL1 equ 1   ; Kernel (OS)
EL2 equ 2   ; Hypervisor (virtualization)
EL3 equ 3   ; Secure Monitor (firmware)

; ── PSTATE fields ──
PSTATE_N  equ (1 shl 31)   ; Negative
PSTATE_Z  equ (1 shl 30)   ; Zero
PSTATE_C  equ (1 shl 29)   ; Carry
PSTATE_V  equ (1 shl 28)   ; Overflow
PSTATE_D  equ (1 shl 9)    ; Debug mask
PSTATE_A  equ (1 shl 8)    ; SError mask
PSTATE_I  equ (1 shl 7)    ; IRQ mask
PSTATE_F  equ (1 shl 6)    ; FIQ mask

; ── Condition codes (for csel/cset/cinc) ──
COND_EQ equ 0x0
COND_NE equ 0x1
COND_CS equ 0x2   ; HS
COND_CC equ 0x3   ; LO
COND_MI equ 0x4
COND_PL equ 0x5
COND_VS equ 0x6
COND_VC equ 0x7
COND_HI equ 0x8
COND_LS equ 0x9
COND_GE equ 0xA
COND_LT equ 0xB
COND_GT equ 0xC
COND_LE equ 0xD
COND_AL equ 0xE
COND_NV equ 0xF

; ── Page Table Descriptor (4 KB granule, 4-level) ──
PT_VALID     equ (1 shl 0)
PT_TABLE     equ (1 shl 1)   ; level 0-2: table descriptor
PT_BLOCK     equ 0           ; level 1-2: block descriptor
PT_AF        equ (1 shl 10)  ; Access Flag
PT_NG        equ (1 shl 11)  ; Not Global
PT_SH_INNER  equ (3 shl 8)   ; Inner Shareable
PT_SH_OUTER  equ (2 shl 8)
PT_SH_NONE   equ (0 shl 8)
PT_AP_RW_EL1 equ (0 shl 6)   ; RW, EL1 only
PT_AP_RW_ALL equ (1 shl 6)   ; RW, EL0+EL1
PT_AP_RO_EL1 equ (2 shl 6)   ; RO, EL1
PT_AP_RO_ALL equ (3 shl 6)   ; RO, EL0+EL1
PT_XN        equ (1 shl 54)  ; Execute Never
PT_UXN       equ (1 shl 54)
PT_PXN       equ (1 shl 53)
PT_MAIR_NORMAL equ 0x04      ; index into MAIR (normal memory)
PT_MAIR_DEVICE  equ 0x00

; ── MAIR attribute indices ──
MAIR_NORMAL_INDX  equ 0x00
MAIR_DEVICE_INDX  equ 0x01

; ── TTBR flags ──
TTBR_CNP  equ (1 shl 0)   ; Common not private
TTBR_SH_INNER equ (3 shl 8)
TTBR_IRGN_WBWA equ (1 shl 6)   ; Inner write-back, write-allocate

; ── TCR flags ──
TCR_T0SZ_48 equ (16 shl 0)   ; 48-bit virtual address (2^48)
TCR_T1SZ_48 equ (16 shl 16)
TCR_IRGN0_WBWA equ (1 shl 8)
TCR_ORGN0_WBWA equ (1 shl 10)
TCR_SH0_INNER  equ (3 shl 12)
TCR_TG0_4K     equ (0 shl 14) ; 4 KB granule
TCR_TG1_4K     equ (2 shl 30)
TCR_IPS_48     equ (5 shl 32) ; 48-bit physical
TCR_ASID_16    equ (1 shl 36)

; ── System registers ──
SYS_SCTLR_EL1   equ sctlr_el1
SYS_TCR_EL1     equ tcr_el1
SYS_TTBR0_EL1   equ ttbr0_el1
SYS_TTBR1_EL1   equ ttbr1_el1
SYS_MAIR_EL1    equ mair_el1
SYS_ELR_EL1     equ elr_el1
SYS_SPSR_EL1    equ spsr_el1
SYS_VBAR_EL1    equ vbar_el1
SYS_CNTPCT_EL0  equ cntpct_el0   ; counter-timer physical count
SYS_CNTFRQ_EL0  equ cntfrq_el0   ; counter-timer frequency
SYS_TPIDR_EL0   equ tpidr_el0    ; thread ID
SYS_TPIDR_EL1   equ tpidr_el1

; ── SCTLR_EL1 bits ──
SCTLR_M   equ (1 shl 0)    ; MMU enable
SCTLR_A   equ (1 shl 1)    ; Alignment check
SCTLR_C   equ (1 shl 2)    ; D-cache enable
SCTLR_SA  equ (1 shl 3)    ; Stack alignment
SCTLR_I   equ (1 shl 12)   ; I-cache enable
SCTLR_WXN equ (1 shl 19)   ; Write permission implies XN
SCTLR_EE  equ (1 shl 25)   ; Endianness (0=LE)

; ── Exception vector offsets (from VBAR) ──
VEC_SYNC_EL1T   equ 0x000   ; Synchronous, EL1 using SP_EL0
VEC_IRQ_EL1T    equ 0x080
VEC_FIQ_EL1T    equ 0x100
VEC_SERR_EL1T   equ 0x180
VEC_SYNC_EL1H   equ 0x200   ; Synchronous, EL1 using SP_EL1
VEC_IRQ_EL1H    equ 0x280
VEC_FIQ_EL1H    equ 0x300
VEC_SERR_EL1H   equ 0x380
VEC_SYNC_EL0_64 equ 0x400   ; Synchronous, EL0 (AArch64)
VEC_IRQ_EL0_64  equ 0x480
VEC_FIQ_EL0_64  equ 0x500
VEC_SERR_EL0_64 equ 0x580
VEC_SYNC_EL0_32 equ 0x600   ; Synchronous, EL0 (AArch32)
VEC_IRQ_EL0_32  equ 0x680
VEC_FIQ_EL0_32  equ 0x700
VEC_SERR_EL0_32 equ 0x780
VEC_ALIGN       equ 0x800   ; vector table size

; ── Barriers ──
macro dsb_arm64 option=sy {
    dsb option
}
macro dmb_arm64 option=sy {
    dmb option
}
macro isb_arm64 {
    isb
}

; ── SVC (syscall) — x8 = syscall number ──
macro svc_berry {
    svc #0
}

; ── Cache maintenance (by virtual address) ──
macro dc_civac reg {
    dc civac, reg
}
macro dc_cvau reg {
    dc cvau, reg
}
macro ic_ivau reg {
    ic ivau, reg
}

; ── TLB maintenance ──
macro tlbi_vmalle1 {
    tlbi vmalle1
}
macro tlbi_vaae1 reg {
    tlbi vaae1, reg
}

; ── Vector table entry (branch to handler) ──
macro vec_entry handler {
    b handler
    .align 4
}

; ── Enable MMU (EL1) ──
; ttbr0 = address of level-0 page table, mair = MAIR value
macro enable_mmu_el1 ttbr0 {
    ; invalidate TLB
    tlbi vmalle1
    isb_arm64
    ; set TTBR0
    mov x0, ttbr0
    msr ttbr0_el1, x0
    ; set MAIR (normal + device)
    mov x0, #0x00FF4400      ; device + normal WB
    msr mair_el1, x0
    ; set TCR (48-bit VA, 4K granule, inner shareable)
    mov x0, #(TCR_T0SZ_48 or TCR_IRGN0_WBWA or TCR_ORGN0_WBWA or TCR_SH0_INNER or TCR_TG0_4K or TCR_IPS_48)
    msr tcr_el1, x0
    ; enable MMU + caches
    mrs x0, sctlr_el1
    orr x0, x0, #(SCTLR_M or SCTLR_C or SCTLR_I)
    msr sctlr_el1, x0
    isb_arm64
}
