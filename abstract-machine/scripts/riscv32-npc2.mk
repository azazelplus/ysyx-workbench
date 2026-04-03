# riscv32-npc2.mk - 使用标准 RV32I 指令集测试 npc2

# 与 minirv-npc2.mk 的区别：
#   - minirv: RV32E (16寄存器) + 软件乘除法 + minirv-gcc 工具链
#   - riscv32: RV32I (32寄存器) + 标准 riscv64-linux-gnu 工具链

include $(AM_HOME)/scripts/isa/riscv.mk
include $(AM_HOME)/scripts/platform/npc2.mk

# 覆盖默认的 64 位配置，切换为 32 位
# 注意：你的 npc2 只实现了 RV32I，没有 M 扩展（乘除法）
# 所以这里用 rv32i 而不是 rv32im
# 用 += 追加，后面的 -march 选项会覆盖前面(riscv.mk中打算使用rv32g)的.
COMMON_CFLAGS += -march=rv32i -mabi=ilp32
#汇编器选项
ASFLAGS       += -march=rv32i -mabi=ilp32    # ASM 也要用 32 位指令集
# 链接器选项
LDFLAGS       += -melf32lriscv

CFLAGS  += -DISA_RISCV32

# 由于 npc2 没有实现 M 扩展（硬件乘除法），需要软件模拟
# 这些库文件是 GCC 标准 libgcc 的一部分.
# 任何 RV32I（不含 M 扩展）的架构都需要这些软件乘除法库
# 包含：__divsi3, __modsi3, __muldi3, __ashldi3 等软件实现
AM_SRCS += riscv/npc/libgcc/div.S \
           riscv/npc/libgcc/muldi3.S \
           riscv/npc/libgcc/multi3.c \
           riscv/npc/libgcc/ashldi3.c \
           riscv/npc/libgcc/unused.c
