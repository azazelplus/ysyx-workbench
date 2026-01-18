#!/usr/bin/env python3
"""
生成简单的 MiniRV 测试程序（二进制文件）
不需要 RISC-V 工具链，直接输出机器码

测试程序功能：
1. LUI x1, 0x12345   # x1 = 0x12345000
2. ADDI x1, x1, 0x678 # x1 = 0x12345678
3. ADDI x2, x0, 100   # x2 = 100
4. ADD x3, x1, x2     # x3 = x1 + x2
5. SUB x4, x1, x2     # x4 = x1 - x2
6. SW x1, 0(x0)       # mem[0x80000000] = x1 (写到程序起始地址，会覆盖指令，但已经执行过了)
7. LW x5, 0(x0)       # x5 = mem[0x80000000]
8. BEQ x5, x1, +8     # 如果 x5 == x1, 跳过下一条
9. ADDI x6, x0, 0xFF  # x6 = 0xFF (如果跳转失败才执行)
10. EBREAK            # 结束仿真
"""

import struct
import sys

def encode_r_type(opcode, rd, funct3, rs1, rs2, funct7):
    """R-type: funct7[31:25] | rs2[24:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]"""
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode

def encode_i_type(opcode, rd, funct3, rs1, imm):
    """I-type: imm[31:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]"""
    imm = imm & 0xFFF  # 12-bit immediate
    return (imm << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode

def encode_s_type(opcode, funct3, rs1, rs2, imm):
    """S-type: imm[11:5][31:25] | rs2[24:20] | rs1[19:15] | funct3[14:12] | imm[4:0][11:7] | opcode[6:0]"""
    imm = imm & 0xFFF
    imm_11_5 = (imm >> 5) & 0x7F
    imm_4_0 = imm & 0x1F
    return (imm_11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (imm_4_0 << 7) | opcode

def encode_b_type(opcode, funct3, rs1, rs2, imm):
    """B-type: imm[12|10:5][31:25] | rs2[24:20] | rs1[19:15] | funct3[14:12] | imm[4:1|11][11:7] | opcode[6:0]"""
    imm = imm & 0x1FFE  # 13-bit, LSB always 0
    imm_12 = (imm >> 12) & 0x1
    imm_11 = (imm >> 11) & 0x1
    imm_10_5 = (imm >> 5) & 0x3F
    imm_4_1 = (imm >> 1) & 0xF
    return (imm_12 << 31) | (imm_10_5 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (imm_4_1 << 8) | (imm_11 << 7) | opcode

def encode_u_type(opcode, rd, imm):
    """U-type: imm[31:12] | rd[11:7] | opcode[6:0]"""
    imm = imm & 0xFFFFF  # 20-bit immediate
    return (imm << 12) | (rd << 7) | opcode

# RISC-V opcodes
OP_LUI    = 0b0110111
OP_AUIPC  = 0b0010111
OP_JAL    = 0b1101111
OP_JALR   = 0b1100111
OP_BRANCH = 0b1100011
OP_LOAD   = 0b0000011
OP_STORE  = 0b0100011
OP_IMM    = 0b0010011
OP_REG    = 0b0110011
OP_SYSTEM = 0b1110011

# 生成测试程序
program = []

# 1. LUI x1, 0x12345  (x1 = 0x12345000)
program.append(encode_u_type(OP_LUI, rd=1, imm=0x12345))

# 2. ADDI x1, x1, 0x678  (x1 = 0x12345678)
program.append(encode_i_type(OP_IMM, rd=1, funct3=0b000, rs1=1, imm=0x678))

# 3. ADDI x2, x0, 100  (x2 = 100)
program.append(encode_i_type(OP_IMM, rd=2, funct3=0b000, rs1=0, imm=100))

# 4. ADD x3, x1, x2  (x3 = x1 + x2)
program.append(encode_r_type(OP_REG, rd=3, funct3=0b000, rs1=1, rs2=2, funct7=0b0000000))

# 5. SUB x4, x1, x2  (x4 = x1 - x2)
program.append(encode_r_type(OP_REG, rd=4, funct3=0b000, rs1=1, rs2=2, funct7=0b0100000))

# 6. SW x1, 0x100(x0)  存到 0x80000100，不覆盖指令
program.append(encode_s_type(OP_STORE, funct3=0b010, rs1=0, rs2=1, imm=0x100))

# 7. LW x5, 0x100(x0)  (x5 = mem[0x80000100])
program.append(encode_i_type(OP_LOAD, rd=5, funct3=0b010, rs1=0, imm=0x100))

# 8. BEQ x5, x1, +8  如果 x5 == x1，跳过下一条指令（跳到 EBREAK）
program.append(encode_b_type(OP_BRANCH, funct3=0b000, rs1=5, rs2=1, imm=8))

# 9. ADDI x6, x0, 0xFF  (如果跳转失败才执行，x6 = 255)
program.append(encode_i_type(OP_IMM, rd=6, funct3=0b000, rs1=0, imm=0xFF))

# 10. EBREAK (0x00100073)
program.append(0x00100073)

# 输出
output_file = sys.argv[1] if len(sys.argv) > 1 else "test.bin"

with open(output_file, "wb") as f:
    for inst in program:
        f.write(struct.pack("<I", inst))  # Little-endian 32-bit

print(f"Generated {output_file} with {len(program)} instructions:")
for i, inst in enumerate(program):
    print(f"  {i*4:3d}: 0x{inst:08x}")
