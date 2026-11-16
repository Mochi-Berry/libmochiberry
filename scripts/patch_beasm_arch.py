#!/usr/bin/env python3
"""Patch beasm.c to add multi-architecture support — v3 (fixed ordering)."""
import sys

with open('beasm/beasm.c', 'r') as f:
    src = f.read()

# ===== 1. Add arch enum after g_file =====
arch_enum = '''
/* Multi-architecture target selection */
enum {
    ARCH_X86_64  = 0,
    ARCH_X86_32  = 1,
    ARCH_X86_16  = 2,
    ARCH_ARM32   = 3,
    ARCH_ARM64   = 4,
    ARCH_RISCV32 = 5,
    ARCH_RISCV64 = 6,
    ARCH_RISCV128= 7,
};
static int g_arch = ARCH_X86_64;
static int g_bits = 64;
'''

if 'ARCH_X86_64' not in src:
    marker = 'static const char *g_file = "<input>";'
    idx = src.find(marker)
    if idx >= 0:
        end = idx + len(marker)
        src = src[:end] + '\n' + arch_enum + '\n' + src[end:]
        print("OK: inserted arch enum")
    else:
        print("FATAL: marker not found"); sys.exit(1)
else:
    print("SKIP: arch enum already present")

# ===== 2. Add register tables BEFORE emit_byte =====
reg_tables = '''
/* ---- ARM32 register table ---- */
typedef struct { const char *name; uint8_t code; } arm32_reg_t;
static const arm32_reg_t g_arm32_regs[] = {
    {"r0",0},{"r1",1},{"r2",2},{"r3",3},{"r4",4},{"r5",5},{"r6",6},{"r7",7},
    {"r8",8},{"r9",9},{"r10",10},{"r11",11},{"r12",12},{"r13",13},{"r14",14},{"r15",15},
    {"fp",11},{"ip",12},{"sp",13},{"lr",14},{"pc",15},{"sl",10},
};
static const arm32_reg_t *arm32_reg_lookup(const char *name) {
    for (size_t i = 0; i < sizeof(g_arm32_regs)/sizeof(g_arm32_regs[0]); i++)
        if (strcmp(g_arm32_regs[i].name, name) == 0) return &g_arm32_regs[i];
    return 0;
}

/* ---- ARM64 register table ---- */
typedef struct { const char *name; uint8_t code; int is_w; } arm64_reg_t;
static const arm64_reg_t g_arm64_regs[] = {
    {"x0",0,0},{"x1",1,0},{"x2",2,0},{"x3",3,0},{"x4",4,0},{"x5",5,0},{"x6",6,0},{"x7",7,0},
    {"x8",8,0},{"x9",9,0},{"x10",10,0},{"x11",11,0},{"x12",12,0},{"x13",13,0},{"x14",14,0},{"x15",15,0},
    {"x16",16,0},{"x17",17,0},{"x18",18,0},{"x19",19,0},{"x20",20,0},{"x21",21,0},{"x22",22,0},{"x23",23,0},
    {"x24",24,0},{"x25",25,0},{"x26",26,0},{"x27",27,0},{"x28",28,0},{"x29",29,0},{"x30",30,0},{"x31",31,0},
    {"w0",0,1},{"w1",1,1},{"w2",2,1},{"w3",3,1},{"w4",4,1},{"w5",5,1},{"w6",6,1},{"w7",7,1},
    {"w8",8,1},{"w9",9,1},{"w10",10,1},{"w11",11,1},{"w12",12,1},{"w13",13,1},{"w14",14,1},{"w15",15,1},
    {"w16",16,1},{"w17",17,1},{"w18",18,1},{"w19",19,1},{"w20",20,1},{"w21",21,1},{"w22",22,1},{"w23",23,1},
    {"w24",24,1},{"w25",25,1},{"w26",26,1},{"w27",27,1},{"w28",28,1},{"w29",29,1},{"w30",30,1},{"wzr",31,1},
    {"xzr",31,0},{"sp",31,0},{"lr",30,0},{"fp",29,0},
};
static const arm64_reg_t *arm64_reg_lookup(const char *name) {
    for (size_t i = 0; i < sizeof(g_arm64_regs)/sizeof(g_arm64_regs[0]); i++)
        if (strcmp(g_arm64_regs[i].name, name) == 0) return &g_arm64_regs[i];
    return 0;
}

/* ---- RISC-V register table ---- */
typedef struct { const char *name; uint8_t code; } riscv_reg_t;
static const riscv_reg_t g_riscv_regs[] = {
    {"x0",0},{"x1",1},{"x2",2},{"x3",3},{"x4",4},{"x5",5},{"x6",6},{"x7",7},
    {"x8",8},{"x9",9},{"x10",10},{"x11",11},{"x12",12},{"x13",13},{"x14",14},{"x15",15},
    {"x16",16},{"x17",17},{"x18",18},{"x19",19},{"x20",20},{"x21",21},{"x22",22},{"x23",23},
    {"x24",24},{"x25",25},{"x26",26},{"x27",27},{"x28",28},{"x29",29},{"x30",30},{"x31",31},
    {"zero",0},{"ra",1},{"sp",2},{"gp",3},{"tp",4},
    {"t0",5},{"t1",6},{"t2",7},{"s0",8},{"fp",8},{"s1",9},
    {"a0",10},{"a1",11},{"a2",12},{"a3",13},{"a4",14},{"a5",15},{"a6",16},{"a7",17},
    {"s2",18},{"s3",19},{"s4",20},{"s5",21},{"s6",22},{"s7",23},{"s8",24},{"s9",25},
    {"s10",26},{"s11",27},{"t3",28},{"t4",29},{"t5",30},{"t6",31},
};
static const riscv_reg_t *riscv_reg_lookup(const char *name) {
    for (size_t i = 0; i < sizeof(g_riscv_regs)/sizeof(g_riscv_regs[0]); i++)
        if (strcmp(g_riscv_regs[i].name, name) == 0) return &g_riscv_regs[i];
    return 0;
}
'''

if 'riscv_reg_t *riscv_reg_lookup' not in src:
    marker = 'static void emit_byte(uint8_t v) {'
    idx = src.find(marker)
    if idx >= 0:
        src = src[:idx] + reg_tables + '\n' + src[idx:]
        print("OK: inserted register tables")
    else:
        print("FATAL: emit_byte marker not found"); sys.exit(1)
else:
    print("SKIP: register tables already present")

# ===== 3. Add encoding functions AFTER emit_u64_plain =====
encoding_funcs = '''
/* ---- ARM32 A32 encoding ---- */
static void arm32_emit_u32(uint32_t w) {
    emit_byte(w & 0xFF); emit_byte((w>>8)&0xFF);
    emit_byte((w>>16)&0xFF); emit_byte((w>>24)&0xFF);
}
static void arm32_dp_imm(uint8_t opc, uint8_t rd, uint8_t rn, uint8_t imm8, uint8_t rot) {
    arm32_emit_u32(0xE2000000 | (opc<<21) | (rn<<16) | (rd<<12) | ((rot&0xF)<<8) | (imm8&0xFF));
}
static void arm32_dp_reg(uint8_t opc, uint8_t rd, uint8_t rn, uint8_t rm, int s) {
    arm32_emit_u32(0xE0000000 | (opc<<21) | (s?(1u<<20):0) | (rn<<16) | (rd<<12) | rm);
}
static void arm32_ldr_pre(int U, int L, uint8_t rn, uint8_t rd, int16_t off) {
    arm32_emit_u32(0xE5000000 | (U<<23) | (L<<20) | (rn<<16) | (rd<<12) | ((uint16_t)off&0xFFF));
}
static void arm32_branch_imm(int link, int32_t off) {
    arm32_emit_u32(0xEA000000 | (link?(1u<<24):0) | ((((uint32_t)off-8)/4)&0x00FFFFFF));
}
static void arm32_bx_reg(uint8_t rm) { arm32_emit_u32(0xE12FFF10 | rm); }
static void arm32_swi(uint32_t imm24) { arm32_emit_u32(0xEF000000 | (imm24&0x00FFFFFF)); }
static void arm32_mul_reg(uint8_t rd, uint8_t rn, uint8_t rm) {
    arm32_emit_u32(0xE0000090 | (rd<<16) | (rn<<12) | (9<<4) | rm);
}
static void arm32_nop(void) { arm32_emit_u32(0xE1A00000); }

/* ---- ARM64 A64 encoding ---- */
static void arm64_emit_u32(uint32_t w) {
    emit_byte(w & 0xFF); emit_byte((w>>8)&0xFF);
    emit_byte((w>>16)&0xFF); emit_byte((w>>24)&0xFF);
}
static void arm64_addsub_imm(int sf, int op, int shift, int imm12, uint8_t rn, uint8_t rd) {
    arm64_emit_u32((sf<<31)|(op<<30)|(0x22<<23)|(shift<<22)|((imm12&0xFFF)<<10)|(rn<<5)|rd);
}
static void arm64_addsub_reg(int sf, int op, uint8_t rd, uint8_t rn, uint8_t rm, int sh, int imm6) {
    arm64_emit_u32((sf<<31)|(op<<30)|(0x50<<24)|(sh<<22)|(rm<<16)|((imm6&0x3F)<<10)|(rn<<5)|rd);
}
static void arm64_log_shift_reg(int sf, int opc, uint8_t rd, uint8_t rn, uint8_t rm, int sh, int imm6) {
    arm64_emit_u32((sf<<31)|(opc<<29)|(0x0A<<24)|(sh<<22)|(rm<<16)|((imm6&0x3F)<<10)|(rn<<5)|rd);
}
static void arm64_movw(int sf, int opc, int hw, int imm16, uint8_t rd) {
    arm64_emit_u32((sf<<31)|(opc<<29)|(0x25<<23)|(hw<<21)|((imm16&0xFFFF)<<5)|rd);
}
static void arm64_ldrstr_uimm(int size, int V, int opc, int imm12, uint8_t rn, uint8_t rt) {
    arm64_emit_u32((size<<30)|(1<<28)|(opc<<22)|((imm12&0xFFF)<<10)|(rn<<5)|rt);
}
static void arm64_b_imm(int imm26) {
    arm64_emit_u32((0x05u<<26)|((uint32_t)(imm26)&0x03FFFFFF));
}
static void arm64_bl_imm(int imm26) {
    arm64_emit_u32((1u<<31)|(0x05u<<26)|((uint32_t)(imm26)&0x03FFFFFF));
}
static void arm64_br_reg(int opc, uint8_t rn) {
    arm64_emit_u32(0xD61F0000 | (opc<<21) | (rn<<5));
}
static void arm64_bcond(int cond, int imm19) {
    arm64_emit_u32(0x54000000 | (((uint32_t)imm19&0x7FFFF)<<5) | cond);
}
static void arm64_svc(int imm16) {
    arm64_emit_u32(0xD4000001 | ((imm16&0xFFFF)<<5));
}
static void arm64_nop(void) { arm64_emit_u32(0xD503201F); }
static void arm64_adrp(int immlo, int immhi, uint8_t rd) {
    arm64_emit_u32((1u<<31)|(1<<28)|((immlo&3)<<29)|((immhi&0x7FFFF)<<5)|rd);
}

/* ---- RISC-V encoding ---- */
static void rv_emit_u32(uint32_t w) {
    emit_byte(w & 0xFF); emit_byte((w>>8)&0xFF);
    emit_byte((w>>16)&0xFF); emit_byte((w>>24)&0xFF);
}
static void rv_r(uint32_t f7, uint8_t rs2, uint8_t rs1, uint32_t f3, uint8_t rd, uint32_t opc) {
    rv_emit_u32((f7<<25)|((uint32_t)rs2<<20)|((uint32_t)rs1<<15)|(f3<<12)|((uint32_t)rd<<7)|opc);
}
static void rv_i(int32_t imm, uint8_t rs1, uint32_t f3, uint8_t rd, uint32_t opc) {
    rv_emit_u32(((uint32_t)(imm&0xFFF)<<20)|((uint32_t)rs1<<15)|(f3<<12)|((uint32_t)rd<<7)|opc);
}
static void rv_s(int32_t imm, uint8_t rs2, uint8_t rs1, uint32_t f3, uint32_t opc) {
    uint32_t u=(uint32_t)imm;
    rv_emit_u32(((u>>5)<<25)|((uint32_t)rs2<<20)|((uint32_t)rs1<<15)|(f3<<12)|((u&0x1F)<<7)|opc);
}
static void rv_b(int32_t imm, uint8_t rs2, uint8_t rs1, uint32_t f3, uint32_t opc) {
    uint32_t u=(uint32_t)imm;
    rv_emit_u32((((u>>12)&1)<<31)|(((u>>5)&0x3F)<<25)|((uint32_t)rs2<<20)|((uint32_t)rs1<<15)|
            (f3<<12)|(((u>>1)&0xF)<<8)|(((u>>11)&1)<<7)|opc);
}
static void rv_u(int32_t imm, uint8_t rd, uint32_t opc) {
    rv_emit_u32(((uint32_t)imm&0xFFFFF000)|((uint32_t)rd<<7)|opc);
}
static void rv_j(int32_t imm, uint8_t rd, uint32_t opc) {
    uint32_t u=(uint32_t)imm;
    rv_emit_u32(((u>>20&1)<<31)|(((u>>1)&0x3FF)<<21)|(((u>>11)&1)<<20)|
            (((u>>12)&0xFF)<<12)|((uint32_t)rd<<7)|opc);
}
'''

if 'rv_emit_u32' not in src:
    marker = 'static void emit_reloc_abs'
    idx = src.find(marker)
    if idx >= 0:
        src = src[:idx] + encoding_funcs + '\n' + src[idx:]
        print("OK: inserted encoding functions")
    else:
        print("FATAL: emit_reloc_abs marker not found"); sys.exit(1)
else:
    print("SKIP: encoding functions already present")

# ===== 4. Add .arch directive BEFORE 'format' directive =====
arch_directive = '''
    if (strcmp(mnem, ".arch") == 0 || strcmp(mnem, "arch") == 0) {
        char a[32]; int m = 0;
        while ((args[m] >= 'a' && args[m] <= 'z') || (args[m] >= 'A' && args[m] <= 'Z') || (args[m] >= '0' && args[m] <= '9') || args[m] == '_') {
            if (m < 31) a[m] = args[m]; m++;
        }
        a[m] = 0;
        if (strcmp(a, "x86_64") == 0 || strcmp(a, "amd64") == 0) { g_arch = ARCH_X86_64; g_bits = 64; }
        else if (strcmp(a, "x86_32") == 0 || strcmp(a, "i386") == 0) { g_arch = ARCH_X86_32; g_bits = 32; }
        else if (strcmp(a, "x86_16") == 0 || strcmp(a, "i8086") == 0) { g_arch = ARCH_X86_16; g_bits = 16; }
        else if (strcmp(a, "arm32") == 0 || strcmp(a, "arm") == 0) { g_arch = ARCH_ARM32; g_bits = 32; }
        else if (strcmp(a, "arm64") == 0 || strcmp(a, "aarch64") == 0) { g_arch = ARCH_ARM64; g_bits = 64; }
        else if (strcmp(a, "riscv32") == 0 || strcmp(a, "rv32") == 0) { g_arch = ARCH_RISCV32; g_bits = 32; }
        else if (strcmp(a, "riscv64") == 0 || strcmp(a, "rv64") == 0) { g_arch = ARCH_RISCV64; g_bits = 64; }
        else if (strcmp(a, "riscv128") == 0 || strcmp(a, "rv128") == 0) { g_arch = ARCH_RISCV128; g_bits = 128; }
        else { err("unknown architecture '%s'", a); }
        return;
    }
'''

if 'g_arch = ARCH_ARM32' not in src:
    # Insert BEFORE 'format' directive (not after!)
    marker = '    if (strcmp(mnem, "format") == 0) {'
    idx = src.find(marker)
    if idx >= 0:
        src = src[:idx] + arch_directive + '\n' + src[idx:]
        print("OK: inserted .arch directive (before format)")
    else:
        print("FATAL: format directive marker not found"); sys.exit(1)
else:
    print("SKIP: .arch directive already present")

# ===== 5. Add multi-arch dispatch BEFORE the error line =====
multi_arch = '''
    /* Multi-architecture instruction dispatch */
    if (g_arch == ARCH_ARM32) {
        if (strcmp(mnem, "nop") == 0) { arm32_nop(); return; }
        if (strcmp(mnem, "ret") == 0) { arm32_bx_reg(14); return; }
        if (strcmp(mnem, "b") == 0 && a.kind == OP_IMM) { arm32_branch_imm(0, a.imm); return; }
        if (strcmp(mnem, "bl") == 0 && a.kind == OP_IMM) { arm32_branch_imm(1, a.imm); return; }
        if (strcmp(mnem, "bx") == 0 && a.kind == OP_REG) { arm32_bx_reg(a.reg); return; }
        if (strcmp(mnem, "mov") == 0 && a.kind == OP_REG && b.kind == OP_IMM) { arm32_dp_imm(0xD, a.reg, 0, b.imm & 0xFF, ((uint32_t)b.imm >> 8) & 0xF); return; }
        if (strcmp(mnem, "mov") == 0 && a.kind == OP_REG && b.kind == OP_REG) { arm32_dp_reg(0xD, a.reg, b.reg, 0, 0); return; }
        if (strcmp(mnem, "add") == 0 && a.kind == OP_REG && b.kind == OP_REG && c.kind == OP_IMM) { arm32_dp_imm(4, a.reg, b.reg, c.imm & 0xFF, 0); return; }
        if (strcmp(mnem, "add") == 0 && a.kind == OP_REG && b.kind == OP_REG) { arm32_dp_reg(4, a.reg, a.reg, b.reg, 0); return; }
        if (strcmp(mnem, "sub") == 0 && a.kind == OP_REG && b.kind == OP_REG && c.kind == OP_IMM) { arm32_dp_imm(2, a.reg, b.reg, c.imm & 0xFF, 0); return; }
        if (strcmp(mnem, "sub") == 0 && a.kind == OP_REG && b.kind == OP_REG) { arm32_dp_reg(2, a.reg, a.reg, b.reg, 0); return; }
        if (strcmp(mnem, "cmp") == 0 && a.kind == OP_REG && b.kind == OP_IMM) { arm32_dp_imm(10, 0, a.reg, b.imm & 0xFF, 0); return; }
        if (strcmp(mnem, "cmp") == 0 && a.kind == OP_REG && b.kind == OP_REG) { arm32_dp_reg(10, 0, a.reg, b.reg, 1); return; }
        if (strcmp(mnem, "cmn") == 0) { arm32_dp_reg(11, 0, a.reg, b.reg, 1); return; }
        if (strcmp(mnem, "tst") == 0) { arm32_dp_reg(8, 0, a.reg, b.reg, 1); return; }
        if (strcmp(mnem, "and") == 0) { arm32_dp_reg(0, a.reg, b.reg, c.kind==OP_REG?c.reg:0, 0); return; }
        if (strcmp(mnem, "orr") == 0) { arm32_dp_reg(12, a.reg, b.reg, c.kind==OP_REG?c.reg:0, 0); return; }
        if (strcmp(mnem, "eor") == 0) { arm32_dp_reg(1, a.reg, b.reg, c.kind==OP_REG?c.reg:0, 0); return; }
        if (strcmp(mnem, "mvn") == 0 && a.kind == OP_REG && b.kind == OP_IMM) { arm32_dp_imm(14, a.reg, 0, b.imm & 0xFF, ((uint32_t)b.imm >> 8) & 0xF); return; }
        if (strcmp(mnem, "mul") == 0 && a.kind == OP_REG && b.kind == OP_REG) { arm32_mul_reg(a.reg, a.reg, b.reg); return; }
        if (strcmp(mnem, "ldr") == 0 && a.kind == OP_REG && b.kind == OP_IMM) { arm32_ldr_pre(1, 1, 15, a.reg, b.imm); return; }
        if (strcmp(mnem, "ldr") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { arm32_ldr_pre(1, 1, b.base, a.reg, b.disp); return; }
        if (strcmp(mnem, "str") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { arm32_ldr_pre(1, 0, b.base, a.reg, b.disp); return; }
        if (strcmp(mnem, "swi") == 0 || strcmp(mnem, "svc") == 0) { arm32_swi(a.imm); return; }
        if (strcmp(mnem, "push") == 0 && a.kind == OP_REG) { arm32_ldr_pre(0, 0, 13, a.reg, -4); return; }
        if (strcmp(mnem, "pop") == 0 && a.kind == OP_REG) { arm32_ldr_pre(1, 1, 13, a.reg, 4); return; }
        if (strcmp(mnem, "wfi") == 0) { arm32_emit_u32(0xE320F003); return; }
        if (strcmp(mnem, "wfe") == 0) { arm32_emit_u32(0xE320F002); return; }
        if (strcmp(mnem, "sev") == 0) { arm32_emit_u32(0xE320F004); return; }
        if (strcmp(mnem, "isb") == 0) { arm32_emit_u32(0xF57FF06F); return; }
        if (strcmp(mnem, "dsb") == 0) { arm32_emit_u32(0xF57FF04F); return; }
        if (strcmp(mnem, "dmb") == 0) { arm32_emit_u32(0xF57FF05F); return; }
        if (strcmp(mnem, "clz") == 0 && a.kind == OP_REG) { arm32_emit_u32(0xE16F0F10 | ((uint32_t)a.reg<<12)); return; }
    }

    if (g_arch == ARCH_ARM64) {
        if (strcmp(mnem, "nop") == 0) { arm64_nop(); return; }
        if (strcmp(mnem, "ret") == 0) { arm64_br_reg(2, 30); return; }
        if (strcmp(mnem, "b") == 0 && a.kind == OP_IMM) { arm64_b_imm(a.imm / 4); return; }
        if (strcmp(mnem, "bl") == 0 && a.kind == OP_IMM) { arm64_bl_imm(a.imm / 4); return; }
        if (strcmp(mnem, "br") == 0 && a.kind == OP_REG) { arm64_br_reg(0, a.reg); return; }
        if (strcmp(mnem, "blr") == 0 && a.kind == OP_REG) { arm64_br_reg(1, a.reg); return; }
        if (strcmp(mnem, "svc") == 0 || strcmp(mnem, "swi") == 0) { arm64_svc(a.imm); return; }
        if (strcmp(mnem, "mov") == 0 && a.kind == OP_REG && b.kind == OP_IMM) {
            arm64_movw(1, 2, 0, b.imm & 0xFFFF, a.reg);
            if ((b.imm & ~0xFFFFL) != 0) arm64_movw(1, 3, 1, (b.imm>>16)&0xFFFF, a.reg);
            return;
        }
        if (strcmp(mnem, "mov") == 0 && a.kind == OP_REG && b.kind == OP_REG) {
            arm64_log_shift_reg(1, 1, a.reg, 31, b.reg, 0, 0); return;
        }
        if (strcmp(mnem, "add") == 0 && a.kind == OP_REG && b.kind == OP_REG && c.kind == OP_IMM) { arm64_addsub_imm(1, 0, 0, c.imm, b.reg, a.reg); return; }
        if (strcmp(mnem, "add") == 0 && a.kind == OP_REG && b.kind == OP_REG) { arm64_addsub_imm(1, 0, 0, 0, b.reg, a.reg); return; }
        if (strcmp(mnem, "sub") == 0 && a.kind == OP_REG && b.kind == OP_REG && c.kind == OP_IMM) { arm64_addsub_imm(1, 1, 0, c.imm, b.reg, a.reg); return; }
        if (strcmp(mnem, "sub") == 0 && a.kind == OP_REG && b.kind == OP_REG) { arm64_addsub_reg(1, 1, a.reg, b.reg, 0, 0, 0); return; }
        if (strcmp(mnem, "ldr") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { arm64_ldrstr_uimm(3, 0, 1, b.disp/8, b.base, a.reg); return; }
        if (strcmp(mnem, "ldr") == 0 && a.kind == OP_REG && b.kind == OP_IMM) { arm64_ldrstr_uimm(3, 0, 1, b.imm/8, 0, a.reg); return; }
        if (strcmp(mnem, "str") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { arm64_ldrstr_uimm(3, 0, 0, b.disp/8, b.base, a.reg); return; }
        if (strcmp(mnem, "cmp") == 0 && a.kind == OP_REG && b.kind == OP_REG) { arm64_addsub_reg(1, 1, 31, a.reg, b.reg, 0, 0); return; }
        if (strcmp(mnem, "cmp") == 0 && a.kind == OP_REG && b.kind == OP_IMM) { arm64_addsub_imm(1, 1, 0, b.imm, a.reg, 31); return; }
        if (strcmp(mnem, "and") == 0 && a.kind == OP_REG && b.kind == OP_REG) { arm64_log_shift_reg(1, 0, a.reg, b.reg, c.kind==OP_REG?c.reg:0, 0, 0); return; }
        if (strcmp(mnem, "orr") == 0 && a.kind == OP_REG && b.kind == OP_REG) { arm64_log_shift_reg(1, 1, a.reg, b.reg, c.kind==OP_REG?c.reg:0, 0, 0); return; }
        if (strcmp(mnem, "eor") == 0 && a.kind == OP_REG && b.kind == OP_REG) { arm64_log_shift_reg(1, 2, a.reg, b.reg, c.kind==OP_REG?c.reg:0, 0, 0); return; }
        if (strcmp(mnem, "push") == 0 && a.kind == OP_REG) { arm64_addsub_imm(1, 1, 0, 16, 31, 31); arm64_ldrstr_uimm(3, 0, 0, 0, 31, a.reg); return; }
        if (strcmp(mnem, "pop") == 0 && a.kind == OP_REG) { arm64_ldrstr_uimm(3, 0, 1, 0, 31, a.reg); arm64_addsub_imm(1, 0, 0, 16, 31, 31); return; }
        if (strcmp(mnem, "adrp") == 0 && a.kind == OP_REG && b.kind == OP_IMM) { arm64_adrp((b.imm >> 12) & 3, (b.imm >> 14) & 0x7FFFF, a.reg); return; }
        if (strcmp(mnem, "wfi") == 0) { arm64_emit_u32(0xD503207F); return; }
        if (strcmp(mnem, "wfe") == 0) { arm64_emit_u32(0xD503205F); return; }
        if (strcmp(mnem, "sev") == 0) { arm64_emit_u32(0xD503209F); return; }
        if (strcmp(mnem, "isb") == 0) { arm64_emit_u32(0xD5033FDF); return; }
        if (strcmp(mnem, "dsb") == 0) { arm64_emit_u32(0xD5033F9F); return; }
        if (strcmp(mnem, "dmb") == 0) { arm64_emit_u32(0xD5033FBF); return; }
    }

    if (g_arch == ARCH_RISCV32 || g_arch == ARCH_RISCV64 || g_arch == ARCH_RISCV128) {
        uint32_t opc_op   = 0x33;
        uint32_t opc_opw  = 0x3B;
        uint32_t opc_imm  = 0x13;
        uint32_t opc_immw = 0x1B;
        uint32_t opc_load = 0x03;
        uint32_t opc_store= 0x23;
        uint32_t opc_branch=0x63;
        uint32_t opc_jalr = 0x67;
        uint32_t opc_jal  = 0x6F;
        uint32_t opc_lui  = 0x37;
        uint32_t opc_auipc= 0x17;
        uint32_t opc_system=0x73;

        if (strcmp(mnem, "nop") == 0) { rv_i(0, 0, 0, 0, opc_imm); return; }
        if (strcmp(mnem, "ret") == 0) { rv_i(0, 1, 0, 0, opc_jalr); return; }
        if (strcmp(mnem, "mv") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(0, b.reg, 0, a.reg, opc_imm); return; }
        if (strcmp(mnem, "not") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(-1, b.reg, 7, a.reg, opc_imm); return; }
        if (strcmp(mnem, "neg") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0x20, b.reg, 0, 0, a.reg, opc_op); return; }
        if (strcmp(mnem, "li") == 0 && a.kind == OP_REG && b.kind == OP_IMM) { rv_i(b.imm & 0xFFF, 0, 0, a.reg, opc_imm); return; }
        if (strcmp(mnem, "j") == 0 && a.kind == OP_IMM) { rv_j(a.imm, 0, opc_jal); return; }
        if (strcmp(mnem, "jr") == 0 && a.kind == OP_REG) { rv_i(0, a.reg, 0, 0, opc_jalr); return; }
        if (strcmp(mnem, "call") == 0 && a.kind == OP_IMM) { rv_j(a.imm, 1, opc_jal); return; }
        if (strcmp(mnem, "tail") == 0 && a.kind == OP_IMM) { rv_j(a.imm, 0, opc_jal); return; }

        /* ALU reg-reg */
        if (strcmp(mnem, "add") == 0 && a.kind == OP_REG && b.kind == OP_REG && c.kind == OP_REG) { rv_r(0, c.reg, b.reg, 0, a.reg, opc_op); return; }
        if (strcmp(mnem, "add") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0, b.reg, a.reg, 0, a.reg, opc_op); return; }
        if (strcmp(mnem, "sub") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0x20, b.reg, a.reg, 0, a.reg, opc_op); return; }
        if (strcmp(mnem, "and") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0, b.reg, a.reg, 7, a.reg, opc_op); return; }
        if (strcmp(mnem, "or") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0, b.reg, a.reg, 6, a.reg, opc_op); return; }
        if (strcmp(mnem, "xor") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0, b.reg, a.reg, 4, a.reg, opc_op); return; }
        if (strcmp(mnem, "sll") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0, b.reg, a.reg, 1, a.reg, opc_op); return; }
        if (strcmp(mnem, "srl") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0, b.reg, a.reg, 5, a.reg, opc_op); return; }
        if (strcmp(mnem, "sra") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0x20, b.reg, a.reg, 5, a.reg, opc_op); return; }
        if (strcmp(mnem, "slt") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0, b.reg, a.reg, 2, a.reg, opc_op); return; }
        if (strcmp(mnem, "sltu") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0, b.reg, a.reg, 3, a.reg, opc_op); return; }

        /* ALU reg-imm */
        if (strcmp(mnem, "addi") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(c.imm, b.reg, 0, a.reg, opc_imm); return; }
        if (strcmp(mnem, "addi") == 0 && a.kind == OP_REG && b.kind == OP_IMM) { rv_i(b.imm, a.reg, 0, a.reg, opc_imm); return; }
        if (strcmp(mnem, "slti") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(c.imm, b.reg, 2, a.reg, opc_imm); return; }
        if (strcmp(mnem, "sltiu") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(c.imm, b.reg, 3, a.reg, opc_imm); return; }
        if (strcmp(mnem, "xori") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(c.imm, b.reg, 4, a.reg, opc_imm); return; }
        if (strcmp(mnem, "ori") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(c.imm, b.reg, 6, a.reg, opc_imm); return; }
        if (strcmp(mnem, "andi") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(c.imm, b.reg, 7, a.reg, opc_imm); return; }
        if (strcmp(mnem, "slli") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(c.imm & 0x1F, b.reg, 1, a.reg, opc_imm); return; }
        if (strcmp(mnem, "srli") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(c.imm & 0x1F, b.reg, 5, a.reg, opc_imm); return; }
        if (strcmp(mnem, "srai") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(0x400 | (c.imm & 0x1F), b.reg, 5, a.reg, opc_imm); return; }

        /* M extension */
        if (strcmp(mnem, "mul") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(1, b.reg, a.reg, 0, a.reg, opc_op); return; }
        if (strcmp(mnem, "mulh") == 0) { rv_r(1, b.reg, a.reg, 1, a.reg, opc_op); return; }
        if (strcmp(mnem, "mulhu") == 0) { rv_r(1, b.reg, a.reg, 3, a.reg, opc_op); return; }
        if (strcmp(mnem, "div") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(1, b.reg, a.reg, 4, a.reg, opc_op); return; }
        if (strcmp(mnem, "divu") == 0) { rv_r(1, b.reg, a.reg, 5, a.reg, opc_op); return; }
        if (strcmp(mnem, "rem") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(1, b.reg, a.reg, 6, a.reg, opc_op); return; }
        if (strcmp(mnem, "remu") == 0) { rv_r(1, b.reg, a.reg, 7, a.reg, opc_op); return; }

        /* RV64I W-instructions */
        if (g_arch >= ARCH_RISCV64) {
            if (strcmp(mnem, "addw") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0, b.reg, a.reg, 0, a.reg, opc_opw); return; }
            if (strcmp(mnem, "subw") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_r(0x20, b.reg, a.reg, 0, a.reg, opc_opw); return; }
            if (strcmp(mnem, "addiw") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(c.imm, b.reg, 0, a.reg, opc_immw); return; }
            if (strcmp(mnem, "sllw") == 0) { rv_r(0, b.reg, a.reg, 1, a.reg, opc_opw); return; }
            if (strcmp(mnem, "srlw") == 0) { rv_r(0, b.reg, a.reg, 5, a.reg, opc_opw); return; }
            if (strcmp(mnem, "sraw") == 0) { rv_r(0x20, b.reg, a.reg, 5, a.reg, opc_opw); return; }
        }

        /* Loads */
        if (strcmp(mnem, "lb") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { rv_i(b.disp, b.base, 0, a.reg, opc_load); return; }
        if (strcmp(mnem, "lh") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { rv_i(b.disp, b.base, 1, a.reg, opc_load); return; }
        if (strcmp(mnem, "lw") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { rv_i(b.disp, b.base, 2, a.reg, opc_load); return; }
        if (strcmp(mnem, "lbu") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { rv_i(b.disp, b.base, 4, a.reg, opc_load); return; }
        if (strcmp(mnem, "lhu") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { rv_i(b.disp, b.base, 5, a.reg, opc_load); return; }
        if (g_arch >= ARCH_RISCV64 && strcmp(mnem, "ld") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { rv_i(b.disp, b.base, 3, a.reg, opc_load); return; }

        /* Stores */
        if (strcmp(mnem, "sb") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { rv_s(b.disp, a.reg, b.base, 0, opc_store); return; }
        if (strcmp(mnem, "sh") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { rv_s(b.disp, a.reg, b.base, 1, opc_store); return; }
        if (strcmp(mnem, "sw") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { rv_s(b.disp, a.reg, b.base, 2, opc_store); return; }
        if (g_arch >= ARCH_RISCV64 && strcmp(mnem, "sd") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { rv_s(b.disp, a.reg, b.base, 3, opc_store); return; }

        /* Branches */
        if (strcmp(mnem, "beq") == 0 && c.kind == OP_IMM) { rv_b(c.imm, a.reg, b.reg, 0, opc_branch); return; }
        if (strcmp(mnem, "bne") == 0 && c.kind == OP_IMM) { rv_b(c.imm, a.reg, b.reg, 1, opc_branch); return; }
        if (strcmp(mnem, "blt") == 0 && c.kind == OP_IMM) { rv_b(c.imm, a.reg, b.reg, 4, opc_branch); return; }
        if (strcmp(mnem, "bge") == 0 && c.kind == OP_IMM) { rv_b(c.imm, a.reg, b.reg, 5, opc_branch); return; }
        if (strcmp(mnem, "bltu") == 0 && c.kind == OP_IMM) { rv_b(c.imm, a.reg, b.reg, 6, opc_branch); return; }
        if (strcmp(mnem, "bgeu") == 0 && c.kind == OP_IMM) { rv_b(c.imm, a.reg, b.reg, 7, opc_branch); return; }

        /* JAL/JALR */
        if (strcmp(mnem, "jal") == 0 && a.kind == OP_REG) { rv_j(b.imm, a.reg, opc_jal); return; }
        if (strcmp(mnem, "jalr") == 0 && a.kind == OP_REG && b.kind == OP_MEM) { rv_i(b.disp, b.base, 0, a.reg, opc_jalr); return; }
        if (strcmp(mnem, "jalr") == 0 && a.kind == OP_REG && b.kind == OP_REG) { rv_i(0, b.reg, 0, a.reg, opc_jalr); return; }

        /* Upper immediate */
        if (strcmp(mnem, "lui") == 0 && a.kind == OP_REG) { rv_u(b.imm, a.reg, opc_lui); return; }
        if (strcmp(mnem, "auipc") == 0 && a.kind == OP_REG) { rv_u(b.imm, a.reg, opc_auipc); return; }

        /* System */
        if (strcmp(mnem, "ecall") == 0) { rv_emit_u32(opc_system); return; }
        if (strcmp(mnem, "ebreak") == 0) { rv_emit_u32(opc_system | 1); return; }
        if (strcmp(mnem, "fence") == 0) { rv_emit_u32(0x0FF0000F); return; }
        if (strcmp(mnem, "rdcycle") == 0) { rv_i(0xC00, 0, 0, a.reg, opc_system); return; }
        if (strcmp(mnem, "rdtime") == 0) { rv_i(0xC01, 0, 0, a.reg, opc_system); return; }
        if (strcmp(mnem, "rdinstret") == 0) { rv_i(0xC02, 0, 0, a.reg, opc_system); return; }

        /* CSR */
        if (strcmp(mnem, "csrrw") == 0) { rv_i(a.imm, b.reg, 1, a.reg, opc_system); return; }
        if (strcmp(mnem, "csrrs") == 0) { rv_i(a.imm, b.reg, 2, a.reg, opc_system); return; }
        if (strcmp(mnem, "csrrc") == 0) { rv_i(a.imm, b.reg, 3, a.reg, opc_system); return; }
    }

    if (g_arch == ARCH_X86_16) {
        if (g_bits == 16) {
            if (strcmp(mnem, "int") == 0 && a.kind == OP_IMM) { emit_byte(0xCD); emit_byte((uint8_t)a.imm); return; }
            if (strcmp(mnem, "nop") == 0) { emit_byte(0x90); return; }
            if (strcmp(mnem, "hlt") == 0) { emit_byte(0xF4); return; }
            if (strcmp(mnem, "cli") == 0) { emit_byte(0xFA); return; }
            if (strcmp(mnem, "sti") == 0) { emit_byte(0xFB); return; }
            if (strcmp(mnem, "cld") == 0) { emit_byte(0xFC); return; }
            if (strcmp(mnem, "std") == 0) { emit_byte(0xFD); return; }
            if (strcmp(mnem, "ret") == 0) { emit_byte(0xC3); return; }
            if (strcmp(mnem, "retf") == 0) { emit_byte(0xCB); return; }
            if (strcmp(mnem, "iret") == 0) { emit_byte(0xCF); return; }
            if (strcmp(mnem, "int3") == 0) { emit_byte(0xCC); return; }
            if (strcmp(mnem, "pushf") == 0) { emit_byte(0x9C); return; }
            if (strcmp(mnem, "popf") == 0) { emit_byte(0x9D); return; }
            if (strcmp(mnem, "push") == 0 && a.kind == OP_REG) { emit_byte(0x50 + (a.reg & 7)); return; }
            if (strcmp(mnem, "pop") == 0 && a.kind == OP_REG) { emit_byte(0x58 + (a.reg & 7)); return; }
            if (strcmp(mnem, "mov") == 0 && a.kind == OP_REG && b.kind == OP_IMM) {
                emit_byte(0xB8 + (a.reg & 7));
                emit_byte((uint8_t)(b.imm)); emit_byte((uint8_t)(b.imm >> 8));
                return;
            }
        }
    }
'''

if 'g_arch == ARCH_ARM32)' not in src:
    marker = 'err("unknown instruction or directive'
    idx = src.find(marker)
    if idx >= 0:
        line_start = src.rfind('\n', 0, idx) + 1
        src = src[:line_start] + multi_arch + '\n' + src[line_start:]
        print("OK: inserted multi-arch dispatch")
    else:
        print("FATAL: unknown instruction marker not found"); sys.exit(1)
else:
    print("SKIP: multi-arch dispatch already present")

with open('beasm/beasm.c', 'w') as f:
    f.write(src)

print("beasm.c patched successfully! Lines:", src.count('\n') + 1)
