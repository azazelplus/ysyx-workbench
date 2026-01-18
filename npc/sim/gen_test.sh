#!/bin/bash
# 快速测试脚本：创建一个简单的测试程序

set -e

PROJ_ROOT=$(dirname "$0")/..
cd "$PROJ_ROOT"

mkdir -p test

# 创建一个简单的 RISC-V 汇编测试程序
cat > test/test.S << 'EOF'
# MiniRV 测试程序
# 测试 add, lui, lw, lbu, sw, sb 指令

.section .text
.globl _start

_start:
    # 测试 LUI: 将立即数加载到寄存器高 20 位
    lui x1, 0x12345          # x1 = 0x12345000
    
    # 测试 ADD: 寄存器加法
    addi x2, x0, 100         # x2 = 100
    addi x3, x0, 200         # x3 = 200
    add x4, x2, x3           # x4 = 300
    
    # 测试 SW: 存储字
    lui x5, 0x80000          # x5 = 0x80000000 (存储器基地址)
    addi x5, x5, 0x100       # x5 = 0x80000100
    sw x4, 0(x5)             # mem[0x80000100] = 300
    
    # 测试 LW: 加载字
    lw x6, 0(x5)             # x6 = mem[0x80000100] = 300
    
    # 测试 SB: 存储字节
    addi x7, x0, 0xAB        # x7 = 0xAB
    sb x7, 4(x5)             # mem[0x80000104] = 0xAB
    
    # 测试 LBU: 加载无符号字节
    lbu x8, 4(x5)            # x8 = 0xAB
    
    # 循环：无限循环
loop:
    j loop                   # 跳转到 loop
    
    # 或者使用 EBREAK 终止仿真
    # ebreak
EOF

# 使用 riscv64-unknown-elf-gcc 编译（如果安装了的话）
if command -v riscv64-unknown-elf-gcc &> /dev/null; then
    echo "Compiling test program..."
    riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles \
        -Ttext=0x80000000 -o test/test.elf test/test.S
    riscv64-unknown-elf-objcopy -O binary test/test.elf test/test.bin
    riscv64-unknown-elf-objdump -d test/test.elf > test/test.dump
    echo "Generated: test/test.bin, test/test.elf, test/test.dump"
else
    echo "Warning: riscv64-unknown-elf-gcc not found."
    echo "Please install RISC-V toolchain to compile the test program."
    echo "Assembly source saved to: test/test.S"
fi
