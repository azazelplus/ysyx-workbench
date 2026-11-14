# riscv32-npc.mk - 使用标准 RV32I 指令集测试 npc

include $(AM_HOME)/scripts/isa/riscv.mk
include $(AM_HOME)/scripts/platform/npc.mk

# 覆盖默认的 64 位配置，切换为 32 位
# 客户程序选择 RV32I，适用于核心关闭 M 扩展的配置。
# 用 += 追加，后面的 -march 选项会覆盖前面(riscv.mk中打算使用rv32g)的.
COMMON_CFLAGS += -march=rv32im -mabi=ilp32
#汇编器选项
ASFLAGS       += -march=rv32im -mabi=ilp32    # ASM 也要用 32 位指令集
# 链接器选项
LDFLAGS       += -melf32lriscv

CFLAGS  += -DISA_RISCV32

# RV32I 客户程序使用软件乘除法辅助实现。
# 这些库文件是 GCC 标准 libgcc 的一部分.
# 任何 RV32I（不含 M 扩展）的架构都需要这些软件乘除法库
# 包含：__divsi3, __modsi3, __muldi3, __ashldi3 等软件实现
AM_SRCS += riscv/libgcc/div.S \
           riscv/libgcc/muldi3.S \
           riscv/libgcc/multi3.c \
           riscv/libgcc/ashldi3.c \
           riscv/libgcc/unused.c
