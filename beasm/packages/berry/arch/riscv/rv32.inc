; ═══════════════════════════════════════════════════════════════════════════
; rv32.inc — RISC-V 32-bit (RV32I / RV32IM / RV32IMA)
; ═══════════════════════════════════════════════════════════════════════════
;
; Register: x0-x31 (32-bit), x0 = zero (hardwired)
; ABI: zero, ra, sp, gp, tp, t0-t6, s0-s11, a0-a7
; Instruksi: 32-bit fixed-length, selalu 4-byte aligned
; Syscall: ECALL (a7 = syscall number)
; Calling: RISC-V ABI (a0-a7 args, a0-a1 return, s0-s11 callee-saved)
;
; Format instruksi: R/I/S/B/U/J — semua dibangun dengan bitwise OR.
;
; ═══════════════════════════════════════════════════════════════════════════

use32

; ── Register Aliases (RISC-V ABI) ──
zero equ x0    ; Hardwired zero
ra   equ x1    ; Return address
sp   equ x2    ; Stack pointer
gp   equ x3    ; Global pointer
tp   equ x4    ; Thread pointer
t0   equ x5    ; Temp 0
t1   equ x6    ; Temp 1
t2   equ x7    ; Temp 2
s0   equ x8    ; Saved 0 / Frame pointer (fp)
fp   equ x8
s1   equ x9    ; Saved 1
a0   equ x10   ; Arg 0 / return
a1   equ x11   ; Arg 1 / return
a2   equ x12   ; Arg 2
a3   equ x13   ; Arg 3
a4   equ x14   ; Arg 4
a5   equ x15   ; Arg 5
a6   equ x16   ; Arg 6
a7   equ x17   ; Arg 7 / Syscall ID
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

; ── Opcodes ──
OPC_LOAD    equ 0000011b
OPC_IMM     equ 0010011b
OPC_AUIPC   equ 0010111b
OPC_STORE   equ 0100011b
OPC_R       equ 0110011b
OPC_LUI     equ 0110111b
OPC_BRANCH  equ 1100011b
OPC_JAL     equ 1101111b
OPC_JALR    equ 1100111b
OPC_SYSTEM  equ 1110011b
OPC_MISC    equ 0001111b   ; FENCE
OPC_ATOMIC  equ 0101111b   ; RV32A

; ── funct3 ──
F3_ADD   equ 000b   ; also SUB (funct7 bit5)
F3_SLL   equ 001b
F3_SLT   equ 010b
F3_SLTU  equ 011b
F3_XOR   equ 100b
F3_SRL   equ 101b   ; also SRA
F3_OR    equ 110b
F3_AND   equ 111b

; ── funct3 for loads/stores ──
F3_LB   equ 000b
F3_LH   equ 001b
F3_LW   equ 010b
F3_LBU  equ 100b
F3_LHU  equ 101b
F3_SB   equ 000b
F3_SH   equ 001b
F3_SW   equ 010b

; ── funct3 for branches ──
F3_BEQ  equ 000b
F3_BNE  equ 001b
F3_BLT  equ 100b
F3_BGE  equ 101b
F3_BLTU equ 110b
F3_BGEU equ 111b

; ── funct7 ──
F7_ADD  equ 0000000b
F7_SUB  equ 0100000b
F7_SLL  equ 0000000b
F7_SLT  equ 0000000b
F7_SLTU equ 0000000b
F7_XOR  equ 0000000b
F7_SRL  equ 0000000b
F7_SRA  equ 0100000b
F7_OR   equ 0000000b
F7_AND  equ 0000000b
F7_MUL  equ 0000001b   ; RV32M
F7_DIV  equ 0000001b

; ── SYSTEM instruction funct12 ──
F12_ECALL  equ 000000000000b
F12_EBREAK equ 000000000001b
F12_MRET   equ 001100000010b
F12_SRET   equ 000100000010b
F12_WFI    equ 000100000101b

; ── CSR opcodes (SYSTEM, funct3) ──
F3_CSRRW  equ 001b
F3_CSRRS  equ 010b
F3_CSRRC  equ 011b
F3_CSRRWI equ 101b
F3_CSRRSI equ 110b
F3_CSRRCI equ 111b

; ── Important CSRs (RV32) ──
CSR_MSTATUS    equ 0x300
CSR_MISA       equ 0x301
CSR_MIE        equ 0x304
CSR_MTVEC      equ 0x305
CSR_MSCRATCH   equ 0x340
CSR_MEPC       equ 0x341
CSR_MCAUSE     equ 0x342
CSR_MTVAL      equ 0x343
CSR_MIP        equ 0x344
CSR_MCYCLE     equ 0xB00
CSR_MTIME      equ 0x701   ; (machine timer — MMIO biasa)
CSR_MTIMECMP   equ 0x741
CSR_MHARTID    equ 0xF14
CSR_SSTATUS    equ 0x100
CSR_SIE        equ 0x104
CSR_STVEC      equ 0x105
CSR_SSCRATCH   equ 0x140
CSR_SEPC       equ 0x141
CSR_SCAUSE     equ 0x142
CSR_STVAL      equ 0x143
CSR_SIP        equ 0x144
CSR_SATP       equ 0x180   ; page table base

; ── MSTATUS bits ──
MSTATUS_MIE    equ (1 shl 3)    ; Machine interrupt enable
MSTATUS_MPIE   equ (1 shl 7)    ; Previous MIE
MSTATUS_MPP    equ (3 shl 11)   ; Previous privilege mode
MSTATUS_MPP_M  equ (3 shl 11)
MSTATUS_MPP_S  equ (1 shl 11)
MSTATUS_MPP_U  equ (0 shl 11)
MSTATUS_SIE    equ (1 shl 1)
MSTATUS_SPIE   equ (1 shl 5)
MSTATUS_SPP    equ (1 shl 8)
MSTATUS_SD     equ (1 shl 31)

; ── MCAUSE codes ──
CAUSE_ECALL_M  equ 11
CAUSE_ECALL_S  equ 9
CAUSE_ECALL_U  equ 8
CAUSE_ILLEGAL  equ 2
CAUSE_BREAKPT  equ 3
CAUSE_LOAD_PF  equ 5
CAUSE_STORE_PF equ 7

; ── SATP bits (Sv32) ──
SATP_MODE_SV32 equ (1 shl 31)   ; mode = Sv32
SATP_ASID_SHIFT equ 22
SATP_PPN_MASK  equ 0x003FFFFF

; ── PTE flags (Sv32) ──
PTE_V     equ (1 shl 0)   ; Valid
PTE_R     equ (1 shl 1)   ; Readable
PTE_W     equ (1 shl 2)   ; Writable
PTE_X     equ (1 shl 3)   ; Executable
PTE_U     equ (1 shl 4)   ; User
PTE_G     equ (1 shl 5)   ; Global
PTE_A     equ (1 shl 6)   ; Accessed
PTE_D     equ (1 shl 7)   ; Dirty
PTE_RWX   equ (PTE_R or PTE_W or PTE_X)

; ═══════════════════════════════════════════════════════════════════════════
; R-TYPE (register-register): funct7 | rs2 | rs1 | funct3 | rd | opcode
; ═══════════════════════════════════════════════════════════════════════════

macro _r_type opc, rd, funct3, rs1, rs2, funct7 {
    dd (funct7 shl 25) or (rs2 shl 20) or (rs1 shl 15) or (funct3 shl 12) or (rd shl 7) or opc
}

macro add  rd, rs1, rs2 { _r_type OPC_R, rd, F3_ADD,  rs1, rs2, F7_ADD }
macro sub  rd, rs1, rs2 { _r_type OPC_R, rd, F3_ADD,  rs1, rs2, F7_SUB }
macro sll  rd, rs1, rs2 { _r_type OPC_R, rd, F3_SLL,  rs1, rs2, F7_SLL }
macro slt  rd, rs1, rs2 { _r_type OPC_R, rd, F3_SLT,  rs1, rs2, F7_SLT }
macro sltu rd, rs1, rs2 { _r_type OPC_R, rd, F3_SLTU, rs1, rs2, F7_SLTU }
macro xor  rd, rs1, rs2 { _r_type OPC_R, rd, F3_XOR,  rs1, rs2, F7_XOR }
macro srl  rd, rs1, rs2 { _r_type OPC_R, rd, F3_SRL,  rs1, rs2, F7_SRL }
macro sra  rd, rs1, rs2 { _r_type OPC_R, rd, F3_SRL,  rs1, rs2, F7_SRA }
macro or   rd, rs1, rs2 { _r_type OPC_R, rd, F3_OR,   rs1, rs2, F7_OR }
macro and  rd, rs1, rs2 { _r_type OPC_R, rd, F3_AND,  rs1, rs2, F7_AND }

; ── RV32M (multiplication) ──
macro mul    rd, rs1, rs2 { _r_type OPC_R, rd, 000b, rs1, rs2, 0000001b }
macro mulh   rd, rs1, rs2 { _r_type OPC_R, rd, 001b, rs1, rs2, 0000001b }
macro mulhsu rd, rs1, rs2 { _r_type OPC_R, rd, 010b, rs1, rs2, 0000001b }
macro mulhu  rd, rs1, rs2 { _r_type OPC_R, rd, 011b, rs1, rs2, 0000001b }
macro div    rd, rs1, rs2 { _r_type OPC_R, rd, 100b, rs1, rs2, 0000001b }
macro divu   rd, rs1, rs2 { _r_type OPC_R, rd, 101b, rs1, rs2, 0000001b }
macro rem    rd, rs1, rs2 { _r_type OPC_R, rd, 110b, rs1, rs2, 0000001b }
macro remu   rd, rs1, rs2 { _r_type OPC_R, rd, 111b, rs1, rs2, 0000001b }

; ═══════════════════════════════════════════════════════════════════════════
; I-TYPE (register-immediate): imm[11:0] | rs1 | funct3 | rd | opcode
; ═══════════════════════════════════════════════════════════════════════════

macro _i_type opc, rd, funct3, rs1, imm {
    local _imm
    _imm = imm and 0xFFF
    dd (_imm shl 20) or (rs1 shl 15) or (funct3 shl 12) or (rd shl 7) or opc
}

macro addi  rd, rs1, imm { _i_type OPC_IMM, rd, F3_ADD,  rs1, imm }
macro slti  rd, rs1, imm { _i_type OPC_IMM, rd, F3_SLT,  rs1, imm }
macro sltiu rd, rs1, imm { _i_type OPC_IMM, rd, F3_SLTU, rs1, imm }
macro xori  rd, rs1, imm { _i_type OPC_IMM, rd, F3_XOR,  rs1, imm }
macro ori   rd, rs1, imm { _i_type OPC_IMM, rd, F3_OR,   rs1, imm }
macro andi  rd, rs1, imm { _i_type OPC_IMM, rd, F3_AND,  rs1, imm }
macro slli  rd, rs1, imm { _i_type OPC_IMM, rd, F3_SLL,  rs1, imm }
macro srli  rd, rs1, imm { _i_type OPC_IMM, rd, F3_SRL,  rs1, imm }
macro srai  rd, rs1, imm { _i_type OPC_IMM, rd, F3_SRL,  rs1, imm or (0100000b shl 5) }
macro jalr  rd, rs1, imm { _i_type OPC_JALR, rd, 000b, rs1, imm }

; ── Loads (I-type, LOAD opcode) ──
macro lb  rd, offset, rs1 { _i_type OPC_LOAD, rd, F3_LB,  rs1, offset }
macro lh  rd, offset, rs1 { _i_type OPC_LOAD, rd, F3_LH,  rs1, offset }
macro lw  rd, offset, rs1 { _i_type OPC_LOAD, rd, F3_LW,  rs1, offset }
macro lbu rd, offset, rs1 { _i_type OPC_LOAD, rd, F3_LBU, rs1, offset }
macro lhu rd, offset, rs1 { _i_type OPC_LOAD, rd, F3_LHU, rs1, offset }

; ── CSR instructions (I-type, SYSTEM opcode) ──
macro csrrw rd, rs1, csr { _i_type OPC_SYSTEM, rd, F3_CSRRW,  rs1, csr }
macro csrrs rd, rs1, csr { _i_type OPC_SYSTEM, rd, F3_CSRRS,  rs1, csr }
macro csrrc rd, rs1, csr { _i_type OPC_SYSTEM, rd, F3_CSRRC,  rs1, csr }
macro csrrwi rd, uimm, csr { _i_type OPC_SYSTEM, rd, F3_CSRRWI, uimm, csr }
macro csrrsi rd, uimm, csr { _i_type OPC_SYSTEM, rd, F3_CSRRSI, uimm, csr }
macro csrrci rd, uimm, csr { _i_type OPC_SYSTEM, rd, F3_CSRRC,  uimm, csr }

; ═══════════════════════════════════════════════════════════════════════════
; S-TYPE (store): imm[11:5] | rs2 | rs1 | funct3 | imm[4:0] | opcode
; ═══════════════════════════════════════════════════════════════════════════

macro _s_type opc, funct3, rs1, rs2, imm {
    local _imm, _lo, _hi
    _imm = imm and 0xFFF
    _lo = _imm and 0x1F
    _hi = (_imm shr 5) and 0x7F
    dd (_hi shl 25) or (rs2 shl 20) or (rs1 shl 15) or (funct3 shl 12) or (_lo shl 7) or opc
}

macro sb rs2, offset, rs1 { _s_type OPC_STORE, F3_SB, rs1, rs2, offset }
macro sh rs2, offset, rs1 { _s_type OPC_STORE, F3_SH, rs1, rs2, offset }
macro sw rs2, offset, rs1 { _s_type OPC_STORE, F3_SW, rs1, rs2, offset }

; ═══════════════════════════════════════════════════════════════════════════
; B-TYPE (branch): imm[12|10:5] | rs2 | rs1 | funct3 | imm[4:1|11] | opcode
; ═══════════════════════════════════════════════════════════════════════════

macro _b_type funct3, rs1, rs2, label {
    local _off, _b12, _b11, _b10_5, _b4_1
    _off = label - $
    _b12 = (_off shr 12) and 1
    _b11 = (_off shr 11) and 1
    _b10_5 = (_off shr 5) and 0x3F
    _b4_1 = (_off shr 1) and 0xF
    dd (_b12 shl 31) or (_b10_5 shl 25) or (rs2 shl 20) or (rs1 shl 15) or (funct3 shl 12) or (_b4_1 shl 8) or (_b11 shl 7) or OPC_BRANCH
}

macro beq  rs1, rs2, label { _b_type F3_BEQ,  rs1, rs2, label }
macro bne  rs1, rs2, label { _b_type F3_BNE,  rs1, rs2, label }
macro blt  rs1, rs2, label { _b_type F3_BLT,  rs1, rs2, label }
macro bge  rs1, rs2, label { _b_type F3_BGE,  rs1, rs2, label }
macro bltu rs1, rs2, label { _b_type F3_BLTU, rs1, rs2, label }
macro bgeu rs1, rs2, label { _b_type F3_BGEU, rs1, rs2, label }

; ═══════════════════════════════════════════════════════════════════════════
; U-TYPE (lui/auipc): imm[31:12] | rd | opcode
; ═══════════════════════════════════════════════════════════════════════════

macro lui   rd, imm { dd ((imm and 0xFFFFF) shl 12) or (rd shl 7) or OPC_LUI }
macro auipc rd, imm { dd ((imm and 0xFFFFF) shl 12) or (rd shl 7) or OPC_AUIPC }

; ═══════════════════════════════════════════════════════════════════════════
; J-TYPE (jal): imm[20|10:1|11|19:12] | rd | opcode
; ═══════════════════════════════════════════════════════════════════════════

macro jal rd, label {
    local _off, _j20, _j19_12, _j11, _j10_1
    _off = label - $
    _j20 = (_off shr 20) and 1
    _j19_12 = (_off shr 12) and 0xFF
    _j11 = (_off shr 11) and 1
    _j10_1 = (_off shr 1) and 0x3FF
    dd (_j20 shl 31) or (_j10_1 shl 21) or (_j11 shl 20) or (_j19_12 shl 12) or (rd shl 7) or OPC_JAL
}

; ═══════════════════════════════════════════════════════════════════════════
; SYSTEM instructions
; ═══════════════════════════════════════════════════════════════════════════

macro ecall  { dd (F12_ECALL shl 20) or OPC_SYSTEM }
macro ebreak { dd (F12_EBREAK shl 20) or OPC_SYSTEM }
macro mret   { dd (F12_MRET shl 20) or OPC_SYSTEM }
macro sret   { dd (F12_SRET shl 20) or OPC_SYSTEM }
macro wfi    { dd (F12_WFI shl 20) or OPC_SYSTEM }

; ── Fence ──
macro fence pred, succ {
    ; fm=0, pred, succ, rs1=x0, rd=x0
    local _p, _s
    _p = (pred shl 4) and 0xF0
    _s = succ and 0x0F
    dd (000b shl 28) or (_p shl 24) or (_s shl 20) or OPC_MISC
}
macro fence_i {
    dd (001b shl 28) or (000b shl 20) or OPC_MISC
}

; ═══════════════════════════════════════════════════════════════════════════
; PSEUDO-INSTRUCTIONS
; ═══════════════════════════════════════════════════════════════════════════

macro nop      { addi zero, zero, 0 }
macro mv rd, rs    { addi rd, rs, 0 }
macro not rd, rs   { xori rd, rs, -1 }
macro neg rd, rs   { sub rd, zero, rs }
macro seqz rd, rs  { sltiu rd, rs, 1 }
macro snez rd, rs  { sltu rd, zero, rs }
macro sltz rd, rs  { slt rd, rs, zero }
macro sgtz rd, rs  { slt rd, zero, rs }
macro beqz rs, l   { beq rs, zero, l }
macro bnez rs, l   { bne rs, zero, l }
macro blez rs, l   { bge zero, rs, l }
macro bgez rs, l   { bge rs, zero, l }
macro bltz rs, l   { blt rs, zero, l }
macro bgtz rs, l   { blt zero, rs, l }
macro ret      { jalr zero, ra, 0 }
macro jr rs    { jalr zero, rs, 0 }
macro jal_rs rs, off { jalr ra, rs, off }

; ── li — load immediate (expand by magnitude) ──
macro li rd, imm {
    if imm >= -2048 and imm < 2048
        addi rd, zero, imm
    else if imm and 0xFFF <> 0
        lui rd, (imm shr 12)
        addi rd, rd, (imm and 0xFFF)
    else
        lui rd, (imm shr 12)
    end if
}

; ── la — load address (auipc + addi, PIC-safe) ──
macro la rd, label {
    auipc rd, ((label - $) shr 12) + (((label - $) and 0x800) shr 11)
    addi  rd, rd, (label - $) and 0xFFF
}

; ── call — long call (auipc + jalr) ──
macro call label {
    auipc ra, ((label - $) shr 12) + (((label - $) and 0x800) shr 11)
    jalr  ra, ra, (label - $) and 0xFFF
}
