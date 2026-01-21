#include "disasm.h"
#include <sstream>
#include <iomanip>
#include <vector>

// 寄存器名称
const char* regs[] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

// 提取 bit [high:low]
static inline uint32_t bits(uint32_t inst, int high, int low) {
    return (inst >> low) & ((1 << (high - low + 1)) - 1);
}

// 符号扩展
static inline int32_t sext(uint32_t val, int bits) {
    if (val & (1 << (bits - 1))) {
        return val | -(1 << bits);
    }
    return val;
}

std::string disassemble(uint32_t inst) {
    if (inst == 0) return "NOP (Bubbles)";
    
    std::stringstream ss;
    uint32_t opcode = bits(inst, 6, 0);
    uint32_t rd     = bits(inst, 11, 7);
    uint32_t funct3 = bits(inst, 14, 12);
    uint32_t rs1    = bits(inst, 19, 15);
    uint32_t rs2    = bits(inst, 24, 20);
    uint32_t funct7 = bits(inst, 31, 25);

    // 立即数解码
    int32_t imm_i = sext(bits(inst, 31, 20), 12);
    int32_t imm_s = sext((bits(inst, 31, 25) << 5) | bits(inst, 11, 7), 12);
    int32_t imm_b = sext((bits(inst, 31, 31) << 12) | (bits(inst, 7, 7) << 11) | (bits(inst, 30, 25) << 5) | (bits(inst, 11, 8) << 1), 13);
    int32_t imm_u = sext(bits(inst, 31, 12) << 12, 32); // 注意这里是高20位本身
    int32_t imm_j = sext((bits(inst, 31, 31) << 20) | (bits(inst, 19, 12) << 12) | (bits(inst, 20, 20) << 11) | (bits(inst, 30, 21) << 1), 21);

    auto print_i = [&](const char* name) { ss << name << " " << regs[rd] << ", " << regs[rs1] << ", " << imm_i; };
    auto print_r = [&](const char* name) { ss << name << " " << regs[rd] << ", " << regs[rs1] << ", " << regs[rs2]; };
    auto print_s = [&](const char* name) { ss << name << " " << regs[rs2] << ", " << imm_s << "(" << regs[rs1] << ")"; };
    auto print_l = [&](const char* name) { ss << name << " " << regs[rd] << ", " << imm_i << "(" << regs[rs1] << ")"; };
    auto print_b = [&](const char* name) { ss << name << " " << regs[rs1] << ", " << regs[rs2] << ", " << imm_b; };
    
    switch (opcode) {
        case 0x37: ss << "lui " << regs[rd] << ", " << (bits(inst, 31, 12)); break;
        case 0x17: ss << "auipc " << regs[rd] << ", " << imm_u; break;
        case 0x6f: ss << "jal " << regs[rd] << ", " << imm_j; break;
        case 0x67: ss << "jalr " << regs[rd] << ", " << imm_i << "(" << regs[rs1] << ")"; break;
        
        case 0x63: // Branch
            switch (funct3) {
                case 0: print_b("beq"); break;
                case 1: print_b("bne"); break;
                case 4: print_b("blt"); break;
                case 5: print_b("bge"); break;
                case 6: print_b("bltu"); break;
                case 7: print_b("bgeu"); break;
                default: ss << "unknown-branch";
            }
            break;
            
        case 0x03: // Load
            switch (funct3) {
                case 0: print_l("lb"); break;
                case 1: print_l("lh"); break;
                case 2: print_l("lw"); break;
                case 4: print_l("lbu"); break;
                case 5: print_l("lhu"); break;
                default: ss << "unknown-load";
            }
            break;
            
        case 0x23: // Store
            switch (funct3) {
                case 0: print_s("sb"); break;
                case 1: print_s("sh"); break;
                case 2: print_s("sw"); break;
                default: ss << "unknown-store";
            }
            break;

        case 0x13: // OP-IMM
            switch (funct3) {
                case 0: print_i("addi"); break;
                case 2: print_i("slti"); break;
                case 3: print_i("sltiu"); break;
                case 4: print_i("xori"); break;
                case 6: print_i("ori"); break;
                case 7: print_i("andi"); break;
                case 1: ss << "slli " << regs[rd] << ", " << regs[rs1] << ", " << (inst >> 20 & 0x1F); break;
                case 5: ss << (bits(inst, 30, 30) ? "srai " : "srli ") << regs[rd] << ", " << regs[rs1] << ", " << (inst >> 20 & 0x1F); break;
                default: ss << "unknown-imm";
            }
            break;

        case 0x33: // OP (R-type)
            switch (funct3) { // funct3
                case 0: if (funct7 == 0) print_r("add"); else if (funct7 == 0x20) print_r("sub"); else if (funct7 == 1) print_r("mul"); break;
                case 1: if (funct7 == 0) print_r("sll"); else if (funct7 == 1) print_r("mulh"); break;
                case 2: if (funct7 == 0) print_r("slt"); else if (funct7 == 1) print_r("mulhsu"); break;
                case 3: if (funct7 == 0) print_r("sltu"); else if (funct7 == 1) print_r("mulhu"); break;
                case 4: if (funct7 == 0) print_r("xor"); else if (funct7 == 1) print_r("div"); break;
                case 5: if (funct7 == 0) print_r("srl"); else if (funct7 == 0x20) print_r("sra"); else if (funct7 == 1) print_r("divu"); break;
                case 6: if (funct7 == 0) print_r("or"); else if (funct7 == 1) print_r("rem"); break;
                case 7: if (funct7 == 0) print_r("and"); else if (funct7 == 1) print_r("remu"); break;
                default: ss << "unknown-rtype";
            }
            break;
        case 0x73:
             if (inst == 0x00100073) ss << "ebreak";
             else if (inst == 0x00000073) ss << "ecall";
             else ss << "csrrw/csrrs...";
             break;
        default: ss << "unknown-opcode";
    }

    return ss.str();
}
