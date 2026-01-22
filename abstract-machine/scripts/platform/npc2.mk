# 架构平台mk, 会被$(AM_HOME)/Makefile在参数ARCH = xxx-npc2时include.
# 用于 npc2 单周期处理器

##########################################################################
# 声明/追加定义的变量:
# AM_SRCS       - (Append)  添加该平台(NPC2)所需的底层驱动源文件(.c, .S)
# CFLAGS        - (Append)  添加编译选项
# LDSCRIPTS     - (Append)  指定链接脚本路径
# LDFLAGS       - (Append)  添加链接选项
# NPC2_HOME     - (Define)  定义NPC2项目根目录路径
##########################################################################

# 复用 npc 平台的源文件（单周期与流水线共享底层驱动）
AM_SRCS := riscv/npc/start.S \
           riscv/npc/trm.c \
           riscv/npc/ioe.c \
           riscv/npc/timer.c \
           riscv/npc/input.c \
           riscv/npc/cte.c \
           riscv/npc/trap.S \
           platform/dummy/vme.c \
           platform/dummy/mpe.c

CFLAGS    += -fdata-sections -ffunction-sections
LDSCRIPTS += $(AM_HOME)/scripts/linker.ld
LDFLAGS   += --defsym=_pmem_start=0x80000000 --defsym=_entry_offset=0x0
LDFLAGS   += --gc-sections -e _start

# 关键区别：指向 npc2 目录
NPC2_HOME = $(AM_HOME)/../npc2

MAINARGS_MAX_LEN = 64
MAINARGS_PLACEHOLDER = the_insert-arg_rule_in_Makefile_will_insert_mainargs_here
CFLAGS += -DMAINARGS_MAX_LEN=$(MAINARGS_MAX_LEN) -DMAINARGS_PLACEHOLDER=$(MAINARGS_PLACEHOLDER)

# insert-arg伪目标: 使用insert-arg.py脚本, 将mainargs字符串插入到生成的二进制镜像文件中指定位置.
insert-arg: image
	@python $(AM_HOME)/tools/insert-arg.py $(IMAGE).bin $(MAINARGS_MAX_LEN) $(MAINARGS_PLACEHOLDER) "$(mainargs)"

image: image-dep
	@$(OBJDUMP) -d $(IMAGE).elf > $(IMAGE).txt
	@echo + OBJCOPY "->" $(IMAGE_REL).bin
	@$(OBJCOPY) -S --set-section-flags .bss=alloc,contents -O binary $(IMAGE).elf $(IMAGE).bin

# run目标: 调用NPC2_HOME下的Makefile的run规则, 运行生成的二进制镜像文件.
# 同时传递 ELF 文件路径用于 ftrace
run: insert-arg
	$(MAKE) -C $(NPC2_HOME) run IMG=$(IMAGE).bin ELF=$(IMAGE).elf

.PHONY: insert-arg
