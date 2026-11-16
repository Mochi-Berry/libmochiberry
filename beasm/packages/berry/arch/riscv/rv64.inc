; ═══════════════════════════════════════════════════════════════════════════
; rv64.inc — RISC-V 64-bit (RV64I / RV64IM)
; ═══════════════════════════════════════════════════════════════════════════
;
; Register: x0-x31 (64-bit), x0=zero
; Instruksi: 32-bit fixed-length (ISA selalu aligned)
; Syscall: ECALL
; Calling: RISC-V calling convention (a0-a7 args, a0-a1 return)
;
; Extension dari RV32I:
;   - ADDIW, ADDW, SUBW, SLLW, SRLW, SRAW (32-bit word ops)
;   - LWU, LD, SD (64-bit load/store)
;   - LWU (load word unsigned)
;
; ═══════════════════════════════════════════════════════════════════════════

use64

; ── Register Aliases (sama dengan RV32) ──
zero equ x0
ra   equ x1
sp   equ x2
gp   equ x3
tp   equ x4
t0   equ x5
t1   equ x6
t2   equ x7
s0   equ x8
s1   equ x9
a0   equ x10
a1   equ x11
a2   equ x12
a3   equ x13
a4   equ x14
a5   equ x15
a6   equ x16
a7   equ x17
s2   equ x18
s3   equ x19
s4   equ x20
s5   equ x21
s6   equ x22
s7   equ x23
s8   equ x24
s9   equ x25
s10  equ x26
s11  equ x27
t3   equ x28
t4   equ x29
t5   equ x30
t6   equ x31

; ═══════════════════════════════════════════════════════════════════════════
; RV64I Additional Instructions
; ═══════════════════════════════════════════════════════════════════════════

; W-suffix instructions operate on lower 32 bits, sign-extend result to 64

; ADDIW rd, rs1, imm12 — Add Word Immediate
macro addiw rd, rs1, imm12 {
    local imm
    imm = imm12 and 0xFFF
    dd I_OPCODE or (rd shl 7) or (F3_ADD shl 12) or (rs1 shl 15) or (imm shl 20)
}

; ADDW rd, rs1, rs2 — Add Word (32-bit, sign-extended)
macro addw rd, rs1, rs2 {
    dd R_OPCODE or (rd shl 7) or (F3_ADD shl 12) or (rs1 shl 15) or (rs2 shl 20) or (0000000b shl 25)
}

; SUBW rd, rs1, rs2 — Subtract Word
macro subw rd, rs1, rs2 {
    dd R_OPCODE or (rd shl 7) or (F3_SUB shl 12) or (rs1 shl 15) or (rs2 shl 20) or (0100000b shl 25)
}

; SLLW rd, rs1, rs2 — Shift Left Logical Word
macro sllw rd, rs1, rs2 {
    dd R_OPCODE or (rd shl 7) or (F3_SLL shl 12) or (rs1 shl 15) or (rs2 shl 20) or (0000000b shl 25)
}

; SRLW rd, rs1, rs2 — Shift Right Logical Word
macro srlw rd, rs1, rs2 {
    dd R_OPCODE or (rd shl 7) or (F3_SRL shl 12) or (rs1 shl 15) or (rs2 shl 20) or (0000000b shl 25)
}

; SRAW rd, rs1, rs2 — Shift Right Arithmetic Word
macro sraw rd, rs1, rs2 {
    dd R_OPCODE or (rd shl 7) or (F3_SRA shl 12) or (rs1 shl 15) or (rs2 shl 20) or (0100000b shl 25)
}

; LD rd, offset(rs1) — Load Doubleword (64-bit)
macro ld rd, offset, rs1 {
    local imm
    imm = offset and 0xFFF
    dd LOAD_OPCODE or (rd shl 7) or (011b shl 12) or (rs1 shl 15) or (imm shl 20)
}

; LWU rd, offset(rs1) — Load Word Unsigned (32-bit → 64-bit zero-extend)
macro lwu rd, offset, rs1 {
    local imm
    imm = offset and 0xFFF
    dd LOAD_OPCODE or (rd shl 7) or (110b shl 12) or (rs1 shl 15) or (imm shl 20)
}

; SD rs2, offset(rs1) — Store Doubleword (64-bit)
macro sd rs2, offset, rs1 {
    local imm_lo, imm_hi
    imm_lo = offset and 0x1F
    imm_hi = (offset shr 5) and 0x7F
    dd STORE_OPCODE or (imm_lo shl 7) or (011b shl 12) or (rs1 shl 15) or (rs2 shl 20) or (imm_hi shl 25)
}

; ── Include RV32 base instructions (superset) ──
; (RV64I is superset of RV32I — all RV32 instructions work on RV64)
include 'rv32.inc'
