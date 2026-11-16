; ═══════════════════════════════════════════════════════════════════════════
; arm32.inc — ARM 32-bit (ARMv7 / AArch32 / ARM11/Cortex-A) — ENRICHED
; ═══════════════════════════════════════════════════════════════════════════
;
; Register: R0-R15 (R13=SP, R14=LR, R15=PC)
; Instruksi: 32-bit fixed-length (ARM) / 16-bit (Thumb)
; Conditional execution pada SETIAP instruksi (4 bit cond)
; Barrel shifter pada operand kedua (LSL/LSR/ASR/ROR/RRX)
; Syscall: SVC #0 (Supervisor Call) — r7 = syscall ID
; Calling: AAPCS (R0-R3 args, R0-R1 return, R4-R11 callee-saved)
;
; Data processing: 3-address (Rd, Rn, Operand2) — hasil di Rd
; Load/store: LDR/STR dengan addressing modes + writeback
; Multiply: MUL, MLA, UMULL, UMLAL, SMULL, SMLAL
;
; ═══════════════════════════════════════════════════════════════════════════

use32

; ── Register Aliases (AAPCS) ──
r0  equ r0    ; Arg 1 / return
r1  equ r1    ; Arg 2 / return
r2  equ r2    ; Arg 3
r3  equ r3    ; Arg 4
r4  equ r4    ; Callee-saved
r5  equ r5    ; Callee-saved
r6  equ r6    ; Callee-saved
r7  equ r7    ; Callee-saved / Syscall ID
r8  equ r8    ; Callee-saved
r9  equ r9    ; Platform / callee-saved (SB in some ABIs)
r10 equ r10   ; Callee-saved (SL)
r11 equ r11   ; Frame pointer (FP)
r12 equ r12   ; Intra-procedure scratch (IP)
sp  equ r13   ; Stack pointer
lr  equ r14   ; Link register
pc  equ r15   ; Program counter

; ── Banked registers (privileged modes) ──
; FIQ: r8_fiq..r12_fiq, sp_fiq, lr_fiq
; SVC: sp_svc, lr_svc
; ABT: sp_abt, lr_abt
; IRQ: sp_irq, lr_irq
; UND: sp_und, lr_und

; ── Condition Codes (top 4 bits of every instruction) ──
CC_EQ equ 0x0   ; Equal (Z=1)
CC_NE equ 0x1   ; Not Equal (Z=0)
CC_CS equ 0x2   ; Carry Set / Unsigned >= (C=1)
CC_CC equ 0x3   ; Carry Clear / Unsigned < (C=0)
CC_MI equ 0x4   ; Minus / Negative (N=1)
CC_PL equ 0x5   ; Plus / Positive or Zero (N=0)
CC_VS equ 0x6   ; Overflow (V=1)
CC_VC equ 0x7   ; No Overflow (V=0)
CC_HI equ 0x8   ; Unsigned Higher (C=1 & Z=0)
CC_LS equ 0x9   ; Unsigned Lower or Same (C=0 or Z=1)
CC_GE equ 0xA   ; Signed >= (N=V)
CC_LT equ 0xB   ; Signed < (N!=V)
CC_GT equ 0xC   ; Signed > (Z=0 & N=V)
CC_LE equ 0xD   ; Signed <= (Z=1 or N!=V)
CC_AL equ 0xE   ; Always (default)
CC_NV equ 0xF   ; Never (reserved)

; ── CPSR / SPSR Flags ──
CPSR_N equ (1 shl 31)   ; Negative / Less than
CPSR_Z equ (1 shl 30)   ; Zero
CPSR_C equ (1 shl 29)   ; Carry / Borrow / Extend
CPSR_V equ (1 shl 28)   ; Overflow
CPSR_Q equ (1 shl 27)   ; Saturation (DSP)
CPSR_GE equ (15 shl 16) ; Greater-or-Equal flags (SIMD)
CPSR_E equ (1 shl 9)    ; Endianness (0=LE, 1=BE)
CPSR_A equ (1 shl 8)    ; Asynchronous abort disable
CPSR_I equ (1 shl 7)    ; IRQ disable
CPSR_F equ (1 shl 6)    ; FIQ disable
CPSR_T equ (1 shl 5)    ; Thumb state (1=Thumb)
CPSR_MODE_MASK equ 0x1F

; ── Processor Modes ──
MODE_USR equ 0x10   ; User (unprivileged)
MODE_FIQ equ 0x11   ; Fast Interrupt
MODE_IRQ equ 0x12   ; Interrupt
MODE_SVC equ 0x13   ; Supervisor (kernel)
MODE_MON equ 0x16   ; Monitor (TrustZone)
MODE_ABT equ 0x17   ; Abort
MODE_HYP equ 0x1A   ; Hypervisor (ARMv7)
MODE_UND equ 0x1B   ; Undefined
MODE_SYS equ 0x1F   ; System (same as USR but privileged)

; ── Exception Vectors (0x00000000 / 0xFFFF0000 via SCTLR.V) ──
VEC_RESET    equ 0x00   ; Reset
VEC_UNDEF    equ 0x04   ; Undefined instruction
VEC_SWI      equ 0x08   ; Software interrupt (SVC)
VEC_PREF_ABT equ 0x0C   ; Prefetch abort
VEC_DATA_ABT equ 0x10   ; Data abort
VEC_RESERVED equ 0x14   ; (unused)
VEC_IRQ      equ 0x18   ; IRQ
VEC_FIQ      equ 0x1C   ; FIQ

; ── Barrel Shifter Operation Types ──
SHIFT_LSL equ 0b000   ; Logical Shift Left
SHIFT_LSR equ 0b001   ; Logical Shift Right
SHIFT_ASR equ 0b010   ; Arithmetic Shift Right
SHIFT_ROR equ 0b011   ; Rotate Right
SHIFT_RRX equ 0b011   ; Rotate Right Extended (with #0)

; ── Data Processing Opcodes ──
DP_AND  equ 0b0000   ; Rd = Rn AND Op2
DP_EOR  equ 0b0001   ; Rd = Rn EOR Op2
DP_SUB  equ 0b0010   ; Rd = Rn - Op2
DP_RSB  equ 0b0011   ; Rd = Op2 - Rn (reverse subtract)
DP_ADD  equ 0b0100   ; Rd = Rn + Op2
DP_ADC  equ 0b0101   ; Rd = Rn + Op2 + C (add with carry)
DP_SBC  equ 0b0110   ; Rd = Rn - Op2 + C - 1 (subtract with carry)
DP_RSC  equ 0b0111   ; Rd = Op2 - Rn + C - 1
DP_TST  equ 0b1000   ; flags = Rn AND Op2 (no write)
DP_TEQ  equ 0b1001   ; flags = Rn EOR Op2
DP_CMP  equ 0b1010   ; flags = Rn - Op2
DP_CMN  equ 0b1011   ; flags = Rn + Op2
DP_ORR  equ 0b1100   ; Rd = Rn OR Op2
DP_MOV  equ 0b1101   ; Rd = Op2
DP_BIC  equ 0b1110   ; Rd = Rn AND NOT Op2 (bit clear)
DP_MVN  equ 0b1111   ; Rd = NOT Op2

; ═══════════════════════════════════════════════════════════════════════════
; DATA PROCESSING INSTRUCTIONS (register + optional shift)
; ═══════════════════════════════════════════════════════════════════════════
; Format: cond|00I|opcode|S|Rn|Rd|operand2
; operand2 register: shift_imm(5)|shift_type(2)|0|Rm(4)
; operand2 immediate: rotate_imm(4)|imm8(8) — rotated right by 2*rotate

; ── Core ALU (register) ──
; mov rd, rm
; add rd, rn, rm
; sub rd, rn, rm
; and/orr/eor/bic rd, rn, rm
; rsb rd, rn, rm   (reverse subtract)
; adc/sbc/rsc rd, rn, rm

; ── Core ALU (immediate) ──
; mov rd, #imm
; add rd, rn, #imm
; sub rd, rn, #imm
; cmp rn, #imm     (flags only)
; tst rn, #imm
; teq rn, #imm
; cmn rn, #imm

; ── Comparisons (flags only, no destination) ──
; cmp rn, rm       → flags = rn - rm
; cmn rn, rm       → flags = rn + rm
; tst rn, rm       → flags = rn AND rm
; teq rn, rm       → flags = rn EOR rm

; ── Shifted register operands ──
; add rd, rn, rm, lsl #n
; add rd, rn, rm, lsr #n
; add rd, rn, rm, asr #n
; add rd, rn, rm, ror #n
; add rd, rn, rm, rrx

; ── Register-shifted (shift amount in register) ──
; add rd, rn, rm, lsl rs

; ═══════════════════════════════════════════════════════════════════════════
; MULTIPLY INSTRUCTIONS
; ═══════════════════════════════════════════════════════════════════════════
; mul rd, rm, rs        → Rd = (Rm * Rs)[31:0]
; mla rd, rm, rs, rn    → Rd = (Rm * Rs + Rn)[31:0] (multiply-accumulate)
; umull rdlo, rdhi, rm, rs → 64-bit unsigned: RdHi:RdLo = Rm * Rs
; umlal rdlo, rdhi, rm, rs → unsigned accumulate
; smull rdlo, rdhi, rm, rs → 64-bit signed
; smlal rdlo, rdhi, rm, rs → signed accumulate

; ═══════════════════════════════════════════════════════════════════════════
; LOAD/STORE INSTRUCTIONS
; ═══════════════════════════════════════════════════════════════════════════
; ldr rd, [rn]          → Rd = [Rn]
; ldr rd, [rn, #off]    → Rd = [Rn + off]
; ldr rd, [rn, #-off]!  → pre-indexed writeback: Rn = Rn + off
; ldr rd, [rn], #off    → post-indexed: Rd = [Rn], Rn = Rn + off
; str rd, [rn, rm, lsl #2]  → register offset with shift
; ldrb/ldrh/ldrsb/ldrsh → byte/halfword/sign-extend loads
; strb/strh             → byte/halfword stores
; ldmia rd!, {r0-r3}    → load multiple, increment after, writeback
; stmdb sp!, {r4-r11, lr} → store multiple, decrement before (push)
; ldmia sp!, {r4-r11, pc} → pop + return

; ── Pseudo push/pop ──
; push {r4-r11, lr}     → stmdb sp!, {...}
; pop {r4-r11, pc}      → ldmia sp!, {...}

; ═══════════════════════════════════════════════════════════════════════════
; BRANCH & CONTROL
; ═══════════════════════════════════════════════════════════════════════════
; b label         → branch (PC-relative ±32 MB)
; bl label        → branch with link (call)
; bx rm           → branch and exchange (switch ARM/Thumb)
; blx rm          → branch with link and exchange
; bx lr           → return (pop-style)
; swi #n / svc #n → software interrupt (syscall)
; bkpt #imm       → breakpoint (debug)

; ── Pseudo return ──
; ret             → bx lr (ARM) / mov pc, lr

; ═══════════════════════════════════════════════════════════════════════════
; PS (PROGRAM STATUS) INSTRUCTIONS
; ═══════════════════════════════════════════════════════════════════════════
; mrs rd, cpsr     → read CPSR
; mrs rd, spsr     → read SPSR (privileged)
; msr cpsr_f, rd   → write flags field
; msr cpsr_c, rd   → write control field (change mode)
; msr spsr_f, rd   → write SPSR flags

; ── Mode switch helper ──
; msr cpsr_c, #MODE_SVC | CPSR_I | CPSR_F  → enter SVC mode

; ═══════════════════════════════════════════════════════════════════════════
; SWAP & EXCLUSIVE (synchronization)
; ═══════════════════════════════════════════════════════════════════════════
; swp rd, rm, [rn]      → Rd = [Rn], [Rn] = Rm (ARMv6-)
; swpb rd, rm, [rn]     → byte swap
; ldrex rd, [rn]        → exclusive load (ARMv6+)
; strex rd, rm, [rn]    → exclusive store: Rd = 0 success / 1 fail
; clrex                → clear exclusive monitor

; ═══════════════════════════════════════════════════════════════════════════
; MISC & DSP (ARMv6+)
; ═══════════════════════════════════════════════════════════════════════════
; clz rd, rm          → count leading zeros
; rev rd, rm          → reverse byte order (endian swap)
; rev16 rd, rm        → reverse bytes in 16-bit halfwords
; revsh rd, rm        → reverse bytes, sign-extend halfword
; qadd/qsub rd, rm, rn → saturating add/sub
; uxtb/uxth/sxtb/sxth → extend byte/halfword

; ═══════════════════════════════════════════════════════════════════════════
; COPROCESSOR & SYSTEM
; ═══════════════════════════════════════════════════════════════════════════
; mcr p15, 0, rd, c1, c0, 0  → write CP15 register (system control)
; mrc p15, 0, rd, c1, c0, 0  → read CP15 register
; cdp p14, 0, cd, cn, cm, 0  → coprocessor data processing (VFP)
; ldc/stc p14, ...           → load/store coprocessor (VFP)
; cpsie i                    → enable interrupts (ARMv6+)
; cpsid i                    → disable interrupts
; setend le/be               → set endianness

; ── Interrupt control ──
; cpsie i   → enable IRQ
; cpsie f   → enable FIQ
; cpsid i   → disable IRQ
; cpsid f   → disable FIQ

; ═══════════════════════════════════════════════════════════════════════════
; CP15 System Registers (ARMv7) — mrc/mcr p15
; ═══════════════════════════════════════════════════════════════════════════
; c0:  ID codes (MIDR, CTR, TCMTR, MPIDR)
; c1:  Control (SCTLR, ACTLR)
; c2:  Translation tables (TTBR0, TTBR1, TTBCR)
; c3:  Domain access control (DACR)
; c5:  Fault status (DFSR, IFSR)
; c6:  Fault address (DFAR, IFAR)
; c7:  Cache operations (IALL, DALL, clean/invalidate)
; c8:  TLB operations (TLBIALL, TLBIASID, ...)
; c9:  Cache lockdown / performance
; c10: Memory remap / PRRR
; c12: Vector base (VBAR)
; c13: Process ID (FCSE, CONTEXTIDR)
; c15: Implementation-defined

; ── Common CP15 operations ──
; mrc p15, 0, r0, c0, c0, 0   → read MIDR (main ID)
; mrc p15, 0, r0, c1, c0, 0   → read SCTLR
; mcr p15, 0, r0, c1, c0, 0   → write SCTLR
; mcr p15, 0, r0, c7, c10, 4  → DSB (data sync barrier)
; mcr p15, 0, r0, c7, c10, 5  → DMB (data memory barrier)
; mcr p15, 0, r0, c7, c5, 0   → ICIALLU (invalidate I-cache)
; mcr p15, 0, r0, c8, c7, 0   → TLBIALL (invalidate TLB)
; mcr p15, 0, r0, c12, c0, 0  → write VBAR (vector base)

; ── SCTLR Bits ──
SCTLR_M   equ (1 shl 0)   ; MMU enable
SCTLR_A   equ (1 shl 1)   ; Alignment check enable
SCTLR_C   equ (1 shl 2)   ; D-cache enable
SCTLR_B   equ (1 shl 7)   ; Endianness (1=BE)
SCTLR_S   equ (1 shl 8)   ; System protection
SCTLR_R   equ (1 shl 9)   ; ROM protection
SCTLR_Z   equ (1 shl 11)  ; Branch prediction enable
SCTLR_I   equ (1 shl 12)  ; I-cache enable
SCTLR_V   equ (1 shl 13)  ; Vectors at 0xFFFF0000
SCTLR_RR  equ (1 shl 14)  ; Round-robin cache replacement
SCTLR_HA  equ (1 shl 17)  ; Hardware access flag
SCTLR_WXN equ (1 shl 19)  ; Write permission implies XN
SCTLR_UWXN equ (1 shl 20) ; Unprivileged write implies XN
SCTLR_FI  equ (1 shl 21)  ; Fast interrupts configuration
SCTLR_VE  equ (1 shl 24)  ; Vectored interrupts

; ── TTBCR Bits ──
TTBCR_N   equ (0xF shl 0) ; Split of TTBR0/TTBR1 address space
TTBCR_PD0 equ (1 shl 4)   ; Disable TTBR0 translation
TTBCR_PD1 equ (1 shl 5)   ; Disable TTBR1 translation

; ── DACR (Domain Access Control) ──
; Each domain (0-15) has 2 bits: 00=no access, 01=client, 11=manager
DACR_CLIENT  equ (1 shl 0)  ; domain 0 = client (check AP bits)
DACR_MANAGER equ (3 shl 0)  ; domain 0 = manager (no AP check)

; ═══════════════════════════════════════════════════════════════════════════
; MMU — Page Table Entries (ARMv7)
; ═══════════════════════════════════════════════════════════════════════════
; L1: 4096 entries (16 KB), each 4 bytes — section or table descriptor
; L2: 256 entries (1 KB), each 4 bytes — page descriptor

; ── L1 Section (1 MB) — bits ──
L1_SECTION      equ (1 shl 0)   ; bit0 = section/table selector
L1_S_BUF        equ (1 shl 2)   ; bufferable
L1_S_CACHE      equ (1 shl 3)   ; cacheable
L1_S_AP0        equ (1 shl 10)  ; access permission bit 0
L1_S_AP1        equ (1 shl 11)  ; access permission bit 1
L1_S_TEX        equ (7 shl 12)  ; TEX bits
L1_S_DOMAIN     equ (15 shl 5)  ; domain field
L1_S_XN         equ (1 shl 4)   ; execute never (ARMv6+)
L1_S_NG         equ (1 shl 17)  ; not global

; ── L1 Table (points to L2) ──
L1_TABLE        equ (1 shl 0)   ; table descriptor
L1_T_NS         equ (1 shl 3)   ; non-secure

; ── L2 Page (4 KB) / Large Page (64 KB) — bits ──
L2_PAGE         equ (2 shl 0)   ; 4 KB page descriptor (bits 1:0 = 10)
L2_LARGE        equ (1 shl 0)   ; 64 KB large page (bits 1:0 = 01)
L2_S_BUF        equ (1 shl 2)
L2_S_CACHE      equ (1 shl 3)
L2_AP0          equ (1 shl 4)
L2_AP1          equ (1 shl 5)
L2_S_SHARE      equ (1 shl 9)   ; shareable
L2_TEX          equ (7 shl 6)
L2_XN           equ (1 shl 0)   ; execute never

; ── Access permissions (AP[1:0]) ──
AP_NO_ACCESS   equ 0b00   ; no access
AP_SV_ACCESS   equ 0b01   ; supervisor only
AP_SV_RO       equ 0b10   ; supervisor read/write, user read-only
AP_ALL_ACCESS  equ 0b11   ; supervisor + user read/write

; ── Domain access ──
DOMAIN_NO_ACCESS equ 0b00
DOMAIN_CLIENT    equ 0b01
DOMAIN_RESERVED  equ 0b10
DOMAIN_MANAGER   equ 0b11

; ═══════════════════════════════════════════════════════════════════════════
; INSTRUCTION MACROS (mnemonic → encoding)
; ═══════════════════════════════════════════════════════════════════════════
; NOTE: fasmg mengenali mnemonik ARM secara native.  Makro di bawah adalah
; ALIAS yang menjamin sintaks konsisten + conditional execution helper.

; ── Condition suffix helper ──
; add_eq rd, rn, rm  (execute only if EQ)
; Contoh pemakaian suffix:  moveq r0, #1

; ── Data processing dengan set-flag (S suffix) ──
; adds rd, rn, rm   → ADD + set flags
; cmp rn, rm        → SUB + set flags, no destination
; cmn rn, rm        → ADD + set flags, no destination

; ── Load/Store helpers ──
; ldr rd, [rn, #off]!   pre-index with writeback
; ldr rd, [rn], #off    post-index
; ldr rd, [rn, rm, lsl #2]  scaled register offset
; strb rd, [rn]         byte store
; ldrh rd, [rn]         halfword load
; ldrsb/ldrsh rd, [rn]  sign-extended loads
; ldmia rn!, {list}     load multiple increment after + writeback
; stmdb rn!, {list}     store multiple decrement before (push frame)

; ── Pseudo: push/pop ──
; push {r4-r11, lr}     frame save
; pop {r4-r11, pc}      frame restore + return

; ── Interrupt helpers ──
macro cpsie_i { cpsie i }
macro cpsie_f { cpsie f }
macro cpsid_i { cpsid i }
macro cpsid_f { cpsid f }

; ── Memory barrier helpers ──
macro dmb_arm { dmb sy }
macro dsb_arm { dsb sy }
macro isb_arm { isb }

; ── Read/write SCTLR ──
macro read_sctlr dest {
    mrc p15, 0, dest, c1, c0, 0
}
macro write_sctlr src {
    mcr p15, 0, src, c1, c0, 0
}

; ── Read/write TTBR0 ──
macro read_ttbr0 dest {
    mrc p15, 0, dest, c2, c0, 0
}
macro write_ttbr0 src {
    mcr p15, 0, src, c2, c0, 0
}

; ── Read/write DACR ──
macro read_dacr dest {
    mrc p15, 0, dest, c3, c0, 0
}
macro write_dacr src {
    mcr p15, 0, src, c3, c0, 0
}

; ── Invalidate TLB ──
macro invalidate_tlb {
    mov r0, #0
    mcr p15, 0, r0, c8, c7, 0    ; TLBIALL
    mcr p15, 0, r0, c7, c5, 0    ; ICIALLU
}

; ── Enable MMU ──
; ttbr = base of level-1 page table (16 KB aligned)
macro enable_mmu ttbr {
    invalidate_tlb
    ldr r0, =ttbr
    mcr p15, 0, r0, c2, c0, 0    ; TTBR0
    mov r0, #(DACR_CLIENT)
    mcr p15, 0, r0, c3, c0, 0    ; DACR
    read_sctlr r0
    orr r0, r0, #(SCTLR_M or SCTLR_C or SCTLR_I or SCTLR_Z)
    write_sctlr r0
    isb_arm
}

; ── SVC (syscall) — r7 = syscall number ──
macro svc_berry {
    svc #0
}

; ── Vector table entry (load pc from literal) ──
macro vector_entry target {
    ldr pc, [pc, #-4]
    dd target
}

; ── Exception return helpers ──
macro eret_svc {
    movs pc, lr    ; return from SVC (restore CPSR from SPSR)
}

; ── NOP ──
macro nop_arm {
    mov r0, r0     ; canonical ARM NOP
}

; ── Rotate-right-extended ──
; rrx = ror #0 with carry into bit31 — operand shift type RRX
