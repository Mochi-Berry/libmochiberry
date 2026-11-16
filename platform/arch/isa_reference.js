/**
 * isa_reference.js — Comprehensive ISA reference for MBJS multi-architecture support
 *
 * Covers 7 architecture targets:
 *   1. x86_16   (Intel 8086/80186 — 16-bit real mode)
 *   2. x86      (i386–i686 — 32-bit protected mode)
 *   3. x86_64   (AMD64/Intel 64 — 64-bit long mode)
 *   4. ARM32    (ARMv7-A/R — A32 + Thumb-2/T32)
 *   5. ARM64    (ARMv8-A/AArch64 — A64)
 *   6. RV32     (RISC-V RV32I/G/C — 32-bit)
 *   7. RV64     (RISC-V RV64I/G/C — 64-bit)
 *   8. RV128    (RISC-V RV128I — 128-bit)
 *
 * Source references:
 *   - ARM Architecture Reference Manual (ARMv7-A, ARMv8-A)
 *   - RISC-V Unprivileged ISA Specification v20260120 (RV32I, RV64I, RV128I)
 *   - RISC-V Compressed ISA Extension v2.0 (RV32C, RV64C)
 *
 * Copyright (c) 2024-2026 Berry Lab Foundation — libmochiberry project
 */

"use strict";

/* ======================================================================
 * Architecture registry
 * ====================================================================== */

export const ARCHITECTURES = {
    x86_16:  { name: "x86_16",  bits: 16, endianness: "little", regCount: 8,  insnWidth: "variable (1-6 bytes)",  arch: "Intel 8086/80186/80286",    family: "x86" },
    x86:     { name: "x86",     bits: 32, endianness: "little", regCount: 8,  insnWidth: "variable (1-15 bytes)", arch: "i386/i486/i586/i686",       family: "x86" },
    x86_64:  { name: "x86_64",  bits: 64, endianness: "little", regCount: 16, insnWidth: "variable (1-15 bytes)", arch: "AMD64/Intel 64",            family: "x86" },
    arm32:   { name: "arm32",   bits: 32, endianness: "little", regCount: 16, insnWidth: "4 bytes (A32) / 2-4 bytes (T32)", arch: "ARMv7-A/R (Cortex-A/R)", family: "ARM" },
    arm64:   { name: "arm64",   bits: 64, endianness: "little", regCount: 31, insnWidth: "4 bytes (A64)",         arch: "ARMv8-A (AArch64)",          family: "ARM" },
    rv32:    { name: "rv32",    bits: 32, endianness: "little", regCount: 32, insnWidth: "4 bytes (32-bit) / 2 bytes (C)", arch: "RV32I/G/C (RISC-V 32)", family: "RISC-V" },
    rv64:    { name: "rv64",    bits: 64, endianness: "little", regCount: 32, insnWidth: "4 bytes (32-bit) / 2 bytes (C)", arch: "RV64I/G/C (RISC-V 64)", family: "RISC-V" },
    rv128:   { name: "rv128",   bits: 128,endianness: "little", regCount: 32, insnWidth: "4 bytes (32-bit) / 2 bytes (C)", arch: "RV128I (RISC-V 128)",    family: "RISC-V" },
};

/* ======================================================================
 * ARM32 (ARMv7-A/R) — A32 Instruction Set
 *
 * 32-bit fixed-length instructions in A32 mode.
 * 16/32-bit mixed-length instructions in Thumb-2 (T32) mode.
 *
 * Register file:
 *   r0-r12   — general purpose (r13=SP, r14=LR, r15=PC)
 *   CPSR     — current program status register (N,Z,C,V,Q,I,F,T,M[4:0])
 *   d0-d31   — VFP/NEON double-precision floating point
 *   s0-s31   — VFP single-precision floating point
 *
 * Encoding formats:
 *   Data Processing:  cond(4) | 00 I(1) opcode(4) S(1) Rn(4) Rd(4) operand2(12)
 *   Load/Store:       cond(4) | 01 I(1) P(1) U(1) B(1) W(1) L(1) Rn(4) Rd(4) offset(12)
 *   Branch:           cond(4) | 101 L(1) offset(24)
 *   Multiply:         cond(4) | 0000 00AS Rd(4) Rs(4) Rn(4) 1001 Rm(4)
 *   Coprocessor:      cond(4) | 1110 ...
 *   Supervisor Call:  cond(4) | 1111 imm24
 *
 * Condition codes (cond field, 4 bits):
 *   EQ=0000 NE=0001 CS/HS=0010 CC/LO=0011 MI=0100 PL=0101 VS=0110 VC=0111
 *   HI=1000 LS=1001 GE=1010 LT=1011 GT=1100 LE=1101 AL=1110 NV=1111(ARMv7: PLDW)
 *
 * Data Processing opcodes (bits 24-21):
 *   AND=0000 EOR=0001 SUB=0010 RSB=0011 ADD=0100 ADC=0101 SBC=0110 RSC=0111
 *   TST=1000 TEQ=1001 CMP=1010 CMN=1011 ORR=1100 MOV=1101 BIC=1110 MVN=1111
 * ====================================================================== */

export const ARM32 = {
    registers: {
        gp: ["r0","r1","r2","r3","r4","r5","r6","r7","r8","r9","r10","r11","r12","sp","lr","pc"],
        sp: "r13", lr: "r14", pc: "r15",
        fpscr: "fpscr",
        aliases: { fp: "r11", ip: "r12", sb: "r9", sl: "r10" }
    },

    conditions: {
        EQ: 0x0, NE: 0x1, CS: 0x2, CC: 0x3, MI: 0x4, PL: 0x5, VS: 0x6, VC: 0x7,
        HI: 0x8, LS: 0x9, GE: 0xA, LT: 0xB, GT: 0xC, LE: 0xD, AL: 0xE
    },

    dpOpcodes: {
        AND: 0x0, EOR: 0x1, SUB: 0x2, RSB: 0x3, ADD: 0x4, ADC: 0x5, SBC: 0x6, RSC: 0x7,
        TST: 0x8, TEQ: 0x9, CMP: 0xA, CMN: 0xB, ORR: 0xC, MOV: 0xD, BIC: 0xE, MVN: 0xF
    },

    // Barrel shifter types for operand2 encoding
    shiftTypes: { LSL: 0, LSR: 1, ASR: 2, ROR: 3 },

    /**
     * Encode ARM32 A32 data processing instruction
     * @param {string} opcode - AND/EOR/SUB/ADD/ORR/MOV/etc
     * @param {number} rd - destination register (0-15)
     * @param {number} rn - first source register (0-15)
     * @param {object} operand2 - { type: 'imm', value } or { type: 'reg', Rm, shift, shiftImm }
     * @param {object} opts - { s: set flags, cond: condition code }
     * @returns {number} 32-bit instruction word
     */
    encodeDP(opcode, rd, rn, operand2, opts = {}) {
        const cond = (opts.cond !== undefined) ? (ARM32.conditions[opts.cond] || 0xE) : 0xE;
        const opcodeBits = ARM32.dpOpcodes[opcode];
        if (opcodeBits === undefined) throw new Error(`ARM32: unknown DP opcode ${opcode}`);
        const S = opts.s ? 1 : 0;
        let I, op2;

        if (operand2.type === 'imm') {
            // Immediate: rotate right by 2*rot4
            I = 1;
            const v = operand2.value & 0xFFFFFFFF;
            let rot = operand2.rot || 0;
            // Find optimal rotation
            if (v === 0) { op2 = 0; rot = 0; }
            else if (rot) { op2 = ((v >>> (rot * 2)) | (v << (32 - rot * 2))) >>> 0; }
            else { op2 = v; rot = 0; }
            op2 = ((rot & 0xF) << 8) | (op2 & 0xFF);
        } else {
            // Register with shift
            I = 0;
            const Rm = operand2.Rm || 0;
            const shiftType = ARM32.shiftTypes[operand2.shift || 'LSL'];
            const shiftImm = operand2.shiftImm || 0;
            op2 = (shiftType << 5) | ((shiftImm & 0x1F) << 7) | (Rm);
        }

        return ((cond & 0xF) << 28) | (I << 25) | (opcodeBits << 21) | (S << 20) |
               ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | (op2 & 0xFFF);
    },

    /**
     * Encode ARM32 A32 branch instruction
     * @param {number} offset - signed 26-bit word offset (relative to PC+8)
     * @param {object} opts - { link: BL, cond }
     * @returns {number} 32-bit instruction word
     */
    encodeBranch(offset, opts = {}) {
        const cond = (opts.cond !== undefined) ? (ARM32.conditions[opts.cond] || 0xE) : 0xE;
        const L = opts.link ? 1 : 0;
        const imm24 = (offset >> 2) & 0xFFFFFF;
        return ((cond & 0xF) << 28) | (0xA << 24) | (L << 24) | (imm24 & 0xFFFFFF);
    },

    /**
     * Encode ARM32 load/store instruction
     * @param {string} opcode - LDR/STR/LDRB/STRB
     * @param {number} rd - register to load/store
     * @param {number} rn - base register
     * @param {object} offset - { imm: signed12 } or { shift, Rm }
     * @param {object} opts - { pre: pre-indexed, add: add offset, wb: writeback, cond, byte }
     * @returns {number} 32-bit instruction word
     */
    encodeLoadStore(opcode, rd, rn, offset, opts = {}) {
        const cond = (opts.cond !== undefined) ? (ARM32.conditions[opts.cond] || 0xE) : 0xE;
        const isLoad = opcode.startsWith('L');
        const B = opts.byte ? 1 : 0;
        const P = opts.pre !== false ? 1 : 0;
        const U = opts.add !== false ? 1 : 0;
        const W = opts.wb ? 1 : 0;
        const L = isLoad ? 1 : 0;
        let offsetBits;

        if (offset.imm !== undefined) {
            // Immediate offset
            offsetBits = (offset.imm < 0) ? (0x1000000 | ((-offset.imm) & 0xFFF)) : (offset.imm & 0xFFF);
        } else {
            // Register offset
            const Rm = offset.Rm || 0;
            const shiftType = ARM32.shiftTypes[offset.shift || 'LSL'];
            offsetBits = (shiftType << 5) | ((offset.shiftImm || 0) << 7) | Rm;
            offsetBits |= (1 << 25); // Register offset bit
        }

        return ((cond & 0xF) << 28) | (0x5 << 25) | (P << 24) | (U << 23) | (B << 22) |
               (W << 21) | (L << 20) | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) |
               (offsetBits & 0xFFF);
    },

    /**
     * Encode ARM32 multiply instruction
     */
    encodeMultiply(rd, rn, rm, rs, opts = {}) {
        const cond = (opts.cond !== undefined) ? (ARM32.conditions[opts.cond] || 0xE) : 0xE;
        const A = opts.accumulate ? 1 : 0;
        const S = opts.s ? 1 : 0;
        return ((cond & 0xF) << 28) | (0x0 << 24) | (A << 21) | (S << 20) |
               ((rd & 0xF) << 16) | ((rn & 0xF) << 12) | ((rs & 0xF) << 8) |
               (0x9 << 4) | (rm & 0xF);
    },

    /**
     * Encode ARM32 software interrupt (SVC/SWI)
     */
    encodeSWI(imm24, opts = {}) {
        const cond = (opts.cond !== undefined) ? (ARM32.conditions[opts.cond] || 0xE) : 0xE;
        return ((cond & 0xF) << 28) | (0xF << 24) | (imm24 & 0xFFFFFF);
    },

    /**
     * Encode ARM32 MRS (move PSR to register)
     */
    encodeMRS(rd, spsr = false) {
        const PSR = spsr ? 1 : 0;
        return (0xE << 28) | (0x010F0000) | (PSR << 22) | ((rd & 0xF) << 12);
    },

    /**
     * Encode ARM32 MSR (move register to PSR)
     */
    encodeMSR(fieldMask, val, opts = {}) {
        const cond = (opts.cond !== undefined) ? (ARM32.conditions[opts.cond] || 0xE) : 0xE;
        const R = opts.spsr ? 1 : 0;
        const c = (fieldMask & 1) << 19;
        const x = (fieldMask & 2) << 18;
        const s = (fieldMask & 4) << 17;
        const f = (fieldMask & 8) << 16;
        return ((cond & 0xF) << 28) | (0x320F0000) | (R << 22) | (f | s | x | c) | ((val & 0xF) << 12);
    },

    /**
     * Encode Thumb-2 (T32) 16-bit instruction
     */
    encodeThumb16(opcode, opts = {}) {
        return opcode & 0xFFFF;
    },

    /**
     * Encode Thumb-2 (T32) 32-bit instruction (two 16-bit halfwords)
     */
    encodeThumb32(opcode1, opcode2) {
        return [(opcode1 & 0xFFFF), (opcode2 & 0xFFFF)];
    },

    // Common ARM32 instruction patterns for JS-to-native
    patterns: {
        // Linux ARM32 syscalls
        SYS_EXIT:   0x01,  // r7 = 1, swi 0
        SYS_WRITE:  0x04,  // r7 = 4, swi 0
        SYS_READ:   0x03,  // r7 = 3, swi 0
        SYS_OPEN:   0x05,  // r7 = 5, swi 0
        SYS_CLOSE:  0x06,  // r7 = 6, swi 0
        // Berry OS syscalls (hypothetical)
        BERRY_SOW:     0x02,
        BERRY_WITHER:  0x05,
        // Function call convention (AAPCS): r0-r3 args, r0 return, r11=FP, r13=SP
        CALLER_SAVED: ["r0","r1","r2","r3","r12"],
        CALLEE_SAVED: ["r4","r5","r6","r7","r8","r9","r10","r11"],
        ARG_REGS: ["r0","r1","r2","r3"],
        RETURN_REG: "r0",
        FRAME_PTR: "r11",
        STACK_PTR: "r13",
    }
};

/* ======================================================================
 * ARM64 (AArch64 / ARMv8-A) — A64 Instruction Set
 *
 * 32-bit fixed-length instructions.
 *
 * Register file:
 *   x0-x30   — 64-bit general purpose
 *   x31/SP   — stack pointer (when used as dest), zero register (when read)
 *   w0-w30   — 32-bit (lower 32 bits of x-registers, zero-extended to 64)
 *   w31/WZR  — zero register (32-bit write-only)
 *   PC       — program counter (not directly addressable)
 *   NZCV     — condition flags (bit 31=N, 30=Z, 29=C, 28=V)
 *   d0-d31   — 64-bit FP/SIMD
 *   v0-v31   — 128-bit SIMD/FP
 *
 * Encoding formats (32-bit):
 *   Data Processing (Reg):  sf(1) op(1) S(1) 01010 shift(2) 0 Rm(5) imm6(6) Rn(5) Rd(5)
 *   Data Processing (Imm): sf(1) opc(2) 100010 sh(1) imm12(12) Rn(5) Rd(5)
 *   Load/Store:             size(2) 111 V(1) 01 opc(2) Rn(5) Rt(5) (various addr modes)
 *   Branch:                 00101 L(1) imm26(26)
 *   Branch Register:        1101011 0010 opc(4) 11111 0000 00 Rn(5) 00000
 *   Conditional:            sf(1) 01010100 Rm(5) 0 cond(4) Rn(5) Rd(5)
 *
 * Condition codes (4 bits):
 *   EQ=0000 NE=0001 CS=0010 CC=0011 MI=0100 PL=0101 VS=0110 VC=0111
 *   HI=1000 LS=1001 GE=1010 LT=1011 GT=1100 LE=1101 AL=1110 NV=1111
 * ====================================================================== */

export const ARM64 = {
    registers: {
        gp64: ["x0","x1","x2","x3","x4","x5","x6","x7","x8","x9","x10","x11","x12",
               "x13","x14","x15","x16","x17","x18","x19","x20","x21","x22","x23",
               "x24","x25","x26","x27","x28","x29","x30"],
        gp32: ["w0","w1","w2","w3","w4","w5","w6","w7","w8","w9","w10","w11","w12",
               "w13","w14","w15","w16","w17","w18","w19","w20","w21","w22","w23",
               "w24","w25","w26","w27","w28","w29","w30"],
        sp: "sp", lr: "x30", fp: "x29",
        zr: "xzr", wzr: "wzr",
        pc: "pc"
    },

    conditions: {
        EQ: 0x0, NE: 0x1, CS: 0x2, CC: 0x3, MI: 0x4, PL: 0x5, VS: 0x6, VC: 0x7,
        HI: 0x8, LS: 0x9, GE: 0xA, LT: 0xB, GT: 0xC, LE: 0xD, AL: 0xE
    },

    /**
     * Get register number from name (x0-x30, w0-w30, sp, xzr)
     */
    regNum(name) {
        if (name === 'sp' || name === 'xzr') return 31;
        if (name === 'wzr') return 31;
        const m = name.match(/^([xw])(\d+)$/);
        if (m) return parseInt(m[2]);
        throw new Error(`ARM64: unknown register ${name}`);
    },

    is64Bit(name) {
        return name.startsWith('x') || name === 'sp';
    },

    /**
     * Encode ARM64 add/sub immediate
     * @param {string} op - ADD/SUB/ADDS/SUBS
     * @param {string} rd - destination register
     * @param {string} rn - first source register
     * @param {number} imm12 - 12-bit immediate (unsigned)
     * @param {object} opts - { shift: 0 or 12 }
     */
    encodeAddSubImm(op, rd, rn, imm12, opts = {}) {
        const sf = ARM64.is64Bit(rd) ? 1 : 0;
        const opcodes = { ADD: 0x0, SUB: 0x2, ADDS: 0x1, SUBS: 0x3 };
        const opc = opcodes[op] || 0;
        const shift = opts.shift || 0;
        const sh = shift ? 1 : 0;
        const Rd = ARM64.regNum(rd);
        const Rn = ARM64.regNum(rn);
        return (sf << 31) | (opc << 29) | (0x22 << 23) | (sh << 22) |
               ((imm12 & 0xFFF) << 10) | (Rn << 5) | Rd;
    },

    /**
     * Encode ARM64 add/sub shifted register
     */
    encodeAddSubShifted(op, rd, rn, rm, opts = {}) {
        const sf = ARM64.is64Bit(rd) ? 1 : 0;
        const opcodes = { ADD: 0x0, SUB: 0x2, ADDS: 0x1, SUBS: 0x3 };
        const opc = opcodes[op] || 0;
        const shift = { LSL: 0, LSR: 1, ASR: 2, ROR: 3 }[opts.shift || 'LSL'];
        const imm6 = opts.shiftImm || 0;
        const S = opts.s ? 1 : 0;
        const Rd = ARM64.regNum(rd);
        const Rn = ARM64.regNum(rn);
        const Rm = ARM64.regNum(rm);
        return (sf << 31) | ((opc >> 1) << 30) | (S << 29) | (0x0B << 24) |
               (shift << 22) | (0 << 21) | (Rm << 16) | (imm6 << 10) | (Rn << 5) | Rd;
    },

    /**
     * Encode ARM64 logical immediate (N:immr:imms encoding)
     */
    encodeLogicalImm(op, rd, rn, immN, immr, imms, opts = {}) {
        const sf = ARM64.is64Bit(rd) ? 1 : 0;
        const opcodes = { AND: 0x0, ORR: 0x1, EOR: 0x2, ANDS: 0x3 };
        const opc = opcodes[op] || 0;
        const Rd = ARM64.regNum(rd);
        const Rn = ARM64.regNum(rn);
        return (sf << 31) | (opc << 29) | (0x24 << 23) | (immN << 22) |
               ((imms & 0x3F) << 16) | ((immr & 0x3F) << 10) | (Rn << 5) | Rd;
    },

    /**
     * Encode ARM64 move wide (MOVZ/MOVK/MOVN)
     */
    encodeMoveWide(op, rd, imm16, hw, opts = {}) {
        const sf = ARM64.is64Bit(rd) ? 1 : 0;
        const opc = { MOVN: 0x0, MOVZ: 0x2, MOVK: 0x3 }[op] || 2;
        const Rd = ARM64.regNum(rd);
        return (sf << 31) | (opc << 29) | (0x25 << 23) | ((hw & 0x3) << 21) |
               ((imm16 & 0xFFFF) << 5) | Rd;
    },

    /**
     * Encode ARM64 branch
     */
    encodeBranch(offset, opts = {}) {
        const L = opts.link ? 1 : 0;
        const imm26 = (offset >> 2) & 0x3FFFFFF;
        return (0x5 << 26) | (L << 31) | (imm26 & 0x3FFFFFF);
    },

    /**
     * Encode ARM64 conditional branch (B.cond)
     */
    encodeBranchCond(offset, cond) {
        const condBits = ARM64.conditions[cond] || 0;
        const imm19 = (offset >> 2) & 0x7FFFF;
        return (0x54 << 24) | (imm19 << 5) | condBits;
    },

    /**
     * Encode ARM64 compare and branch (CBZ/CBNZ)
     */
    encodeCompareBranch(rt, offset, opts = {}) {
        const sf = ARM64.is64Bit(rt) ? 1 : 0;
        const op = opts.nonZero ? 1 : 0;
        const Rt = ARM64.regNum(rt);
        const imm19 = (offset >> 2) & 0x7FFFF;
        return (sf << 31) | (0x1B << 25) | (op << 24) | (imm19 << 5) | Rt;
    },

    /**
     * Encode ARM64 load register (immediate offset)
     */
    encodeLoadImm(rt, rn, imm9, opts = {}) {
        const size = ARM64.is64Bit(rt) ? 3 : 2;
        const V = 0;
        const opc = opts.load ? 1 : 0;
        const signed = opts.signed ? 1 : 0;
        const unscaled = opts.unscaled ? 1 : 0;
        const Rt = ARM64.regNum(rt);
        const Rn = ARM64.regNum(rn);
        if (unscaled) {
            return (size << 30) | (0x1 << 26) | (V << 26) | (opc << 22) |
                   (0 << 24) | ((imm9 & 0x1FF) << 12) | (Rn << 5) | Rt;
        }
        return (size << 30) | (0x1 << 27) | (opc << 22) | (1 << 24) |
               ((imm9 & 0x1FF) << 12) | (Rn << 5) | Rt;
    },

    /**
     * Encode ARM64 store register (immediate offset)
     */
    encodeStoreImm(rt, rn, imm12, opts = {}) {
        const size = ARM64.is64Bit(rt) ? 3 : 2;
        const Rt = ARM64.regNum(rt);
        const Rn = ARM64.regNum(rn);
        return (size << 30) | (0x1 << 27) | (0 << 22) | (1 << 24) |
               ((imm12 & 0xFFF) << 10) | (Rn << 5) | Rt;
    },

    /**
     * Encode ARM64 stp/ldp (store/load pair)
     */
    encodePair(rt1, rt2, rn, imm7, opts = {}) {
        const opc = ARM64.is64Bit(rt1) ? 2 : 2;
        const V = 0;
        const L = opts.load ? 1 : 0;
        const Rt1 = ARM64.regNum(rt1);
        const Rt2 = ARM64.regNum(rt2);
        const Rn = ARM64.regNum(rn);
        const imm7Scaled = (imm7 >> 3) & 0x7F;
        return (opc << 30) | (0x5 << 27) | (V << 26) | (L << 22) |
               (imm7Scaled << 15) | (Rt2 << 10) | (Rn << 5) | Rt1;
    },

    /**
     * Encode ARM64 RET
     */
    encodeRet(rn = 30) {
        return (0xD65F0000) | ((rn & 0x1F) << 5);
    },

    /**
     * Encode ARM64 SVC (supervisor call)
     */
    encodeSVC(imm16) {
        return (0xD4000001) | ((imm16 & 0xFFFF) << 5);
    },

    // Linux ARM64 syscall numbers
    patterns: {
        SYS_EXIT:   93,
        SYS_WRITE:  64,
        SYS_READ:   63,
        SYS_OPENAT: 56,
        SYS_CLOSE:  57,
        BERRY_SOW:     0x02,
        BERRY_WITHER:  0x05,
        CALLER_SAVED: ["x0","x1","x2","x3","x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15"],
        CALLEE_SAVED: ["x19","x20","x21","x22","x23","x24","x25","x26","x27","x28","x29","x30"],
        ARG_REGS: ["x0","x1","x2","x3","x4","x5","x6","x7"],
        RETURN_REG: "x0",
        FRAME_PTR: "x29",
        STACK_PTR: "sp",
        ARG_REGS_32: ["w0","w1","w2","w3","w4","w5","w6","w7"],
    }
};

/* ======================================================================
 * RISC-V 32 (RV32I/G/C) — Base Integer Instruction Set
 *
 * 32-bit fixed-length instructions (+ 16-bit compressed with C extension)
 *
 * Register file:
 *   x0/zero  — hardwired zero
 *   x1/ra    — return address
 *   x2/sp    — stack pointer
 *   x3/gp    — global pointer
 *   x4/tp    — thread pointer
 *   x5-x7    — t0-t2 (temporaries)
 *   x8/s0/fp — frame pointer (saved)
 *   x9/s1    — saved register
 *   x10-x11  — a0-a1 (arguments/return values)
 *   x12-x17  — a2-a7 (arguments)
 *   x18-x27  — s2-s11 (saved registers)
 *   x28-x31  — t3-t6 (temporaries)
 *
 * Instruction formats (32-bit, all fixed width):
 *   R-type: funct7(7) | rs2(5) | rs1(5) | funct3(3) | rd(5) | opcode(7)
 *   I-type: imm[11:0](12) | rs1(5) | funct3(3) | rd(5) | opcode(7)
 *   S-type: imm[11:5](7) | rs2(5) | rs1(5) | funct3(3) | imm[4:0](5) | opcode(7)
 *   B-type: imm[12|10:5](7) | rs2(5) | rs1(5) | funct3(3) | imm[4:1|11](5) | opcode(7)
 *   U-type: imm[31:12](20) | rd(5) | opcode(7)
 *   J-type: imm[20|10:1|11|19:12](20) | rd(5) | opcode(7)
 *
 * Major opcodes (bits [6:0]):
 *   0b0000011 (0x03) LOAD      0b0100011 (0x23) STORE
 *   0b0010011 (0x13) OP-IMM    0b0110011 (0x33) OP
 *   0b0010111 (0x17) AUIPC     0b0110111 (0x37) LUI
 *   0b1100011 (0x63) BRANCH    0b1100111 (0x67) JALR
 *   0b1101111 (0x6F) JAL       0b1110011 (0x73) SYSTEM
 *   0b0001111 (0x0F) FENCE
 *
 * funct3 values for OP-IMM/OP:
 *   ADD/SUB=000 SLL=001 SLT=010 SLTU=011 XOR=100 SRL/SRA=101 OR=110 AND=111
 *
 * M extension (multiply/divide):
 *   funct7=0x01 for: MUL=000 MULH=001 MULHSU=010 MULHU=011 DIV=100 DIVU=101 REM=110 REMU=111
 *
 * F/D extensions (floating point):
 *   Major opcode 0b1010011 (0x53) — single/double FP operations
 *   Major opcode 0b0000111 (0x07) — FP loads
 *   Major opcode 0b0100111 (0x27) — FP stores
 *
 * C extension (compressed, 16-bit):
 *   CR-type: funct4(4) | rd/rs1(5) | rs2(5) | op(2)
 *   CI-type: funct3(3) | imm(1) | rd/rs1(5) | imm(5) | op(2)
 *   CSS-type: funct3(3) | imm(6) | rs2(5) | op(2)
 *   CIW-type: funct3(3) | imm(8) | rd(3) | op(2)
 *   CL-type: funct3(3) | imm(3) | rs1(3) | imm(2) | rd(3) | op(2)
 *   CS-type: funct3(3) | imm(3) | rs1(3) | imm(2) | rs2(3) | op(2)
 *   CB-type: funct3(3) | offset(8) | rs1(3) | funct2(2) | op(2)
 *   CJ-type: funct3(3) | jump_target(11) | op(2)
 * ====================================================================== */

export const RV32 = {
    registers: {
        names: ["zero","ra","sp","gp","tp","t0","t1","t2","s0","s1",
                "a0","a1","a2","a3","a4","a5","a6","a7",
                "s2","s3","s4","s5","s6","s7","s8","s9","s10","s11",
                "t3","t4","t5","t6"],
        aliases: { fp: 8, x0: 0, x1: 1, x2: 2, x3: 3, x4: 4 },
        xlen: 32
    },

    majorOp: {
        LOAD: 0x03, STORE: 0x23, OP_IMM: 0x13, OP: 0x33,
        LUI: 0x37, AUIPC: 0x17, JAL: 0x6F, JALR: 0x67,
        BRANCH: 0x63, SYSTEM: 0x73, FENCE: 0x0F,
        OP_IMM_32: 0x1B, OP_32: 0x3B, // RV64 only
        LOAD_FP: 0x07, STORE_FP: 0x27, OP_FP: 0x53 // F/D extensions
    },

    funct3: {
        ADD_SUB: 0, SLL: 1, SLT: 2, SLTU: 3, XOR: 4, SRL_SRA: 5, OR: 6, AND: 7,
        LB: 0, LH: 1, LW: 2, LBU: 4, LHU: 5,
        SB: 0, SH: 1, SW: 2,
        BEQ: 0, BNE: 1, BLT: 4, BGE: 5, BLTU: 6, BGEU: 7,
        ECALL_EBREAK: 0, CSR: 1 // CSR instructions
    },

    funct7: {
        ADD: 0x00, SUB: 0x20, SRL: 0x00, SRA: 0x20,
        MUL: 0x01, DIV: 0x01, REM: 0x01, // M extension
        FADD: 0x00, FSUB: 0x04, FMUL: 0x08, FDIV: 0x0C, FSQRT: 0x2C, // F extension
        FADD_D: 0x01, FSUB_D: 0x05, FMUL_D: 0x09, FDIV_D: 0x0D, FSQRT_D: 0x2D, // D extension
    },

    /**
     * R-type instruction encoding
     */
    encodeR(funct7, rs2, rs1, funct3, rd, opcode) {
        return ((funct7 & 0x7F) << 25) | ((rs2 & 0x1F) << 20) | ((rs1 & 0x1F) << 15) |
               ((funct3 & 0x7) << 12) | ((rd & 0x1F) << 7) | (opcode & 0x7F);
    },

    /**
     * I-type instruction encoding
     */
    encodeI(imm12, rs1, funct3, rd, opcode) {
        return ((imm12 & 0xFFF) << 20) | ((rs1 & 0x1F) << 15) |
               ((funct3 & 0x7) << 12) | ((rd & 0x1F) << 7) | (opcode & 0x7F);
    },

    /**
     * S-type instruction encoding
     */
    encodeS(imm12, rs2, rs1, funct3, opcode) {
        return (((imm12 >> 5) & 0x7F) << 25) | ((rs2 & 0x1F) << 20) | ((rs1 & 0x1F) << 15) |
               ((funct3 & 0x7) << 12) | ((imm12 & 0x1F) << 7) | (opcode & 0x7F);
    },

    /**
     * B-type instruction encoding (branches)
     */
    encodeB(imm13, rs2, rs1, funct3, opcode) {
        // imm13 = signed 13-bit offset (bit 0 always 0)
        const bit12 = (imm13 >> 12) & 1;
        const bit11 = (imm13 >> 11) & 1;
        const bits10_5 = (imm13 >> 5) & 0x3F;
        const bits4_1 = (imm13 >> 1) & 0xF;
        return (bit12 << 31) | (bits10_5 << 25) | ((rs2 & 0x1F) << 20) | ((rs1 & 0x1F) << 15) |
               ((funct3 & 0x7) << 12) | (bits4_1 << 8) | (bit11 << 7) | (opcode & 0x7F);
    },

    /**
     * U-type instruction encoding (LUI, AUIPC)
     */
    encodeU(imm32, rd, opcode) {
        return (imm32 & 0xFFFFF000) | ((rd & 0x1F) << 7) | (opcode & 0x7F);
    },

    /**
     * J-type instruction encoding (JAL)
     */
    encodeJ(imm21, rd, opcode) {
        // imm21 = signed 21-bit offset (bit 0 always 0)
        const bit20 = (imm21 >> 20) & 1;
        const bits10_1 = (imm21 >> 1) & 0x3FF;
        const bit11 = (imm21 >> 11) & 1;
        const bits19_12 = (imm21 >> 12) & 0xFF;
        return (bit20 << 31) | (bits10_1 << 21) | (bit11 << 20) | (bits19_12 << 12) |
               ((rd & 0x1F) << 7) | (opcode & 0x7F);
    },

    /**
     * Compressed (C extension) 16-bit instruction encoding
     * CR-type: funct4[15:12] | rd/rs1[11:7] | rs2[6:2] | op[1:0]
     */
    encodeCR(funct4, rs1, rs2, op) {
        return ((funct4 & 0xF) << 12) | ((rs1 & 0x1F) << 7) | ((rs2 & 0x1F) << 2) | (op & 0x3);
    },

    /**
     * CI-type compressed: funct3[15:13] | [12] | rd/rs1[11:7] | imm[6:2] | op[1:0]
     */
    encodeCI(funct3, rd, imm7, op) {
        const bit12 = (imm7 >> 6) & 1;
        const imm5 = imm7 & 0x1F;
        return ((funct3 & 0x7) << 13) | (bit12 << 12) | ((rd & 0x1F) << 7) |
               ((imm5 & 0x1F) << 2) | (op & 0x3);
    },

    // Common RISC-V instruction patterns
    patterns: {
        SYS_ECALL:  0x00000073,
        SYS_EBREAK: 0x00100073,
        FENCE:      0x0000000F,
        NOP:        0x00000013, // ADDI x0, x0, 0
        // Linux RV32 syscall numbers
        SYS_EXIT:   93,
        SYS_WRITE:  64,
        SYS_READ:   63,
        BERRY_SOW:     0x02,
        BERRY_WITHER:  0x05,
        // Calling convention (standard RISC-V ABI)
        CALLER_SAVED: [10,11,12,13,14,15,16,17,5,6,7,28,29,30,31],
        CALLEE_SAVED: [8,9,18,19,20,21,22,23,24,25,26,27],
        ARG_REGS: [10,11,12,13,14,15,16,17], // a0-a7
        RETURN_REG: 10, // a0
        FRAME_PTR: 8,   // s0/fp
        STACK_PTR: 2,   // sp
        RETURN_ADDR: 1, // ra
    }
};

/* ======================================================================
 * RISC-V 64 (RV64I/G/C) — 64-bit Base Integer
 *
 * Extends RV32I to 64-bit XLEN. Additional *W instructions for 32-bit ops:
 *   ADDIW, ADDW, SUBW, SLLW, SRLW, SRAW, SLLIW, SRLIW, SRAIW
 *
 * Additional load/store:
 *   LD, LWU, SD
 *
 * Shift amounts use 6 bits (0-63) instead of 5 bits.
 * ====================================================================== */

export const RV64 = {
    ...RV32,  // inherit RV32 base
    registers: { ...RV32.registers, xlen: 64 },

    /**
     * *W instruction encoding (32-bit ops in 64-bit mode)
     * Uses funct7=0x01 (M extension) space for ADDW/SUBW? No — uses OP_32 (0x3B)
     */
    encodeW(funct7, rs2, rs1, funct3, rd, opcode) {
        return RV32.encodeR(funct7, rs2, rs1, funct3, rd, opcode);
    },

    /**
     * Additional RV64I-only instructions
     */
    instructions: {
        // These reuse RV32 encoding but with different opcodes
        ADDIW: (rd, rs1, imm12) => RV32.encodeI(imm12, rs1, 0, rd, 0x1B),
        ADDW:  (rd, rs1, rs2)   => RV32.encodeR(0x00, rs2, rs1, 0, rd, 0x3B),
        SUBW:  (rd, rs1, rs2)   => RV32.encodeR(0x20, rs2, rs1, 0, rd, 0x3B),
        SLLW:  (rd, rs1, rs2)   => RV32.encodeR(0x00, rs2, rs1, 1, rd, 0x3B),
        SRLW:  (rd, rs1, rs2)   => RV32.encodeR(0x00, rs2, rs1, 5, rd, 0x3B),
        SRAW:  (rd, rs1, rs2)   => RV32.encodeR(0x20, rs2, rs1, 5, rd, 0x3B),
        SLLIW: (rd, rs1, shamt) => RV32.encodeI((shamt & 0x1F), rs1, 1, rd, 0x1B),
        SRLIW: (rd, rs1, shamt) => RV32.encodeI((shamt & 0x1F), rs1, 5, rd, 0x1B),
        SRAIW: (rd, rs1, shamt) => RV32.encodeI(0x400 | (shamt & 0x1F), rs1, 5, rd, 0x1B),
        LD:    (rd, rs1, imm12) => RV32.encodeI(imm12, rs1, 3, rd, 0x03),
        LWU:   (rd, rs1, imm12) => RV32.encodeI(imm12, rs1, 6, rd, 0x03),
        SD:    (rs2, rs1, imm12) => RV32.encodeS(imm12, rs2, rs1, 3, 0x23),
    }
};

/* ======================================================================
 * RISC-V 128 (RV128I) — 128-bit Base Integer
 *
 * Extends RV64I to 128-bit XLEN. Very few actual hardware implementations.
 *
 * Additional *D instructions for 64-bit ops in 128-bit mode:
 *   ADDID, ADDD, SUBD, SLLD, SRLD, SRAD, SLLID, SRLID, SRAID
 *
 * Additional load/store:
 *   LQ, SQ, LDU
 *
 * Shift amounts use 7 bits (0-127).
 * The *W instructions now sign-extend from bit 31 to bit 127.
 * The *D instructions sign-extend from bit 63 to bit 127.
 * Uses two additional major opcodes: OP-IMM-64 (0x1B) and OP-64 (0x3B).
 *
 * NOTE: RV128 spec is not frozen yet (as of 2026). These encodings are
 * based on the latest draft from riscv.org.
 * ====================================================================== */

export const RV128 = {
    ...RV64,  // inherit RV64 base
    registers: { ...RV64.registers, xlen: 128 },

    /**
     * RV128-specific *D instruction encoding (64-bit ops in 128-bit mode)
     * Uses OP-IMM-64 (0x1B) and OP-64 (0x3B)
     */
    encodeD(funct7, rs2, rs1, funct3, rd, opcode) {
        return RV32.encodeR(funct7, rs2, rs1, funct3, rd, opcode);
    },

    /**
     * RV128I-only instructions
     */
    instructions: {
        // *D instructions: 64-bit operations in 128-bit mode
        ADDID: (rd, rs1, imm12) => RV32.encodeI(imm12, rs1, 0, rd, 0x1B),
        ADDD:  (rd, rs1, rs2)   => RV32.encodeR(0x00, rs2, rs1, 0, rd, 0x3B),
        SUBD:  (rd, rs1, rs2)   => RV32.encodeR(0x20, rs2, rs1, 0, rd, 0x3B),
        SLLD:  (rd, rs1, rs2)   => RV32.encodeR(0x00, rs2, rs1, 1, rd, 0x3B),
        SRLD:  (rd, rs1, rs2)   => RV32.encodeR(0x00, rs2, rs1, 5, rd, 0x3B),
        SRAD:  (rd, rs1, rs2)   => RV32.encodeR(0x20, rs2, rs1, 5, rd, 0x3B),
        SLLID: (rd, rs1, shamt) => RV32.encodeI((shamt & 0x7F), rs1, 1, rd, 0x1B),
        SRLID: (rd, rs1, shamt) => RV32.encodeI((shamt & 0x7F), rs1, 5, rd, 0x1B),
        SRAID: (rd, rs1, shamt) => RV32.encodeI(0x40 | (shamt & 0x7F), rs1, 5, rd, 0x1B),
        LQ:    (rd, rs1, imm16) => RV32.encodeI(imm16, rs1, 4, rd, 0x03), // opcode in MISC-MEM
        SQ:    (rs2, rs1, imm12) => RV32.encodeS(imm12, rs2, rs1, 4, 0x23),
        LDU:   (rd, rs1, imm12) => RV32.encodeI(imm12, rs1, 7, rd, 0x03),
        // *W instructions sign-extend from bit 31 to bit 127
        ADDIW: (rd, rs1, imm12) => RV32.encodeI(imm12, rs1, 0, rd, 0x1B),
        ADDW:  (rd, rs1, rs2)   => RV32.encodeR(0x00, rs2, rs1, 0, rd, 0x3B),
        SUBW:  (rd, rs1, rs2)   => RV32.encodeR(0x20, rs2, rs1, 0, rd, 0x3B),
    }
};

/* ======================================================================
 * Unified export
 * ====================================================================== */

export const ISA = {
    x86_16: ARCHITECTURES.x86_16,
    x86:    ARCHITECTURES.x86,
    x86_64: ARCHITECTURES.x86_64,
    arm32:  ARM32,
    arm64:  ARM64,
    rv32:   RV32,
    rv64:   RV64,
    rv128:  RV128,
};
