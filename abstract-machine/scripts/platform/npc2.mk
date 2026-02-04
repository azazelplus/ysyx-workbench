# 架构平台mk, 会被$(AM_HOME)/Makefile在参数ARCH = xxx-npc2时include.

# 调用逻辑(以cpu-tests为例. 运行其他测试也很简单)
# 首先执行cpu-test/Makefile的run, 它会构建一个Makefile.[待测试程序], 然后make运行它.
# 这个生成的Makefile.[待测试程序]会include [AM_HOME]/Makefile. 
# 然后[AM_HOME]/Makefile会include /scripts/platform/npc2.mk, 
# 而npc2.mk会$(MAKE) run npc2/Makefile
# 接着, npc2/Makefile会include sim.mk. 这个sim.mk会调用verilator编译出npc2的可执行仿真程序.

# 在这个过程中, 
# make[1]: cpu-test/Makefile
# make[2]: Makefile.[待测试程序]
# make[3]: [AM_HOME]/Makefile(include: /scripts/platform/npc2.mk)
# make[4]: npc2/Makefile(include: )

# 用于 npc2 单周期处理器...

##########################################################################
# 声明/追加定义的变量:
# AM_SRCS       - (Append)  添加该平台(NPC2)所需的底层驱动源文件(.c, .S)
# CFLAGS        - (Append)  添加编译选项
# LDSCRIPTS     - (Append)  指定链接脚本路径
# LDFLAGS       - (Append)  添加链接选项
# NPC2_HOME     - (Define)  定义NPC2项目根目录路径
##########################################################################


$(info )
$(info [MAKE_DEBUG] =================================================)
$(info [MAKE_DEBUG] Current make level $$(MAKELEVEL): $(MAKELEVEL))
$(info [MAKE_DEBUG] Current dir $$(CURDIR):   	$(CURDIR))
$(info [MAKE_DEBUG] $$(MAKEFILE_LIST): 		$(MAKEFILE_LIST))
$(info [MAKE_DEBUG] Current makefile:	$(abspath $(lastword $(MAKEFILE_LIST))))
$(info [MAKE_DEBUG] =================================================)
$(info )


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

# 指向 npc2 目录
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
