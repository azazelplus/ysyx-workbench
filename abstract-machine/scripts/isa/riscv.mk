# 本makefile调用层级: 由 riscv32-nemu.mk 调用

# CROSS_COMPILE: 交叉编译工具链前缀. 使用 riscv64-linux-gnu-gcc 进行编译.
CROSS_COMPILE := riscv64-linux-gnu-

# COMMON_CFLAGS: 处理.c和.s文件时传给gcc的选项
# CFLAGS: 编译.c文件时传给gcc的选项.
# ASFLAGS: 编译.s文件时传给gcc的选项.(gcc包含汇编器)
# LDFLAGS: 链接.o和库时传给ld的选项


# -fno-pic 禁用位置无关代码(PIC)生成. 这是逻辑环境中最高效直接的做法...
# -march是给gcc消费的选项, 指定编译架构. 这里提供默认rv64g, 在riscv32-npc.mk中被覆盖.
# -mcmodel=medany 选择代码模型为`medany`. 它允许程序被链接到内存中任何位置, 只要.data和.text段大小不超过2GB. 这也是为了裸机环境.
COMMON_CFLAGS := -fno-pic -march=rv64g -mcmodel=medany -mstrict-align
CFLAGS        += $(COMMON_CFLAGS) -static
ASFLAGS       += $(COMMON_CFLAGS) -O0
LDFLAGS       += -melf64lriscv

# overwrite ARCH_H defined in $(AM_HOME)/Makefile
ARCH_H := arch/riscv.h



# PIC = position independent code, 位置无关代码. gcc默认启用了PIE(位置无关可执行程序模式), 即开启了PIC.





















