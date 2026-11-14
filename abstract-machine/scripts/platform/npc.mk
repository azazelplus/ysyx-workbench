# run: 为npc这台cpu制作客户程序.bin, 并

# 由[AM_HOME]/Makefile会include.
# 本makefile会$(MAKE) run npc/Makefile, 开一个新make运行npc/Makefile.
# 接着, npc/Makefile会include sim.mk. 这个sim.mk会调用verilator编译出npc的可执行仿真程序.

# 在这个过程中,
# make[1]: cpu-test/Makefile
# make[2]: Makefile.[待测试程序]
# make[3]: [AM_HOME]/Makefile(include: /scripts/platform/npc.mk)
# make[4]: npc/Makefile(include: )

# 用于 npc 多周期握手处理器...

##########################################################################
# 声明/追加定义的变量:
# AM_SRCS       - (Append)  添加该平台(NPC)所需的底层驱动源文件(.c, .S)
# CFLAGS        - (Append)  添加编译选项
# LDSCRIPTS     - (Append)  指定链接脚本路径
# LDFLAGS       - (Append)  添加链接选项
# NPC_HOME     - (Define)  定义NPC项目根目录路径
##########################################################################


$(info )
$(info [MAKE_DEBUG] =================================================)
$(info [MAKE_DEBUG] Current make level $$(MAKELEVEL): $(MAKELEVEL))
$(info [MAKE_DEBUG] Current dir $$(CURDIR):   	$(CURDIR))
$(info [MAKE_DEBUG] $$(MAKEFILE_LIST): 		$(MAKEFILE_LIST))
$(info [MAKE_DEBUG] Current makefile:	$(abspath $(lastword $(MAKEFILE_LIST))))
$(info [MAKE_DEBUG] =================================================)
$(info )


NPC_HOME := $(abspath $(AM_HOME)/../npc)
NPC_CONFIG_MK := $(abspath $(AM_HOME)/scripts/platform/npc.mk)

# NPC 软件平台布局：用于 AM 链接、设备驱动和 C++ 仿真器。
NPC_RAM_BASE := 0x80000000
NPC_RAM_SIZE := 0x08000000
NPC_UART_BASE := 0x10000000
NPC_RTC_BASE := 0xa0000048

NPC_CPPFLAGS := -DNPC_RAM_BASE=$(NPC_RAM_BASE)UL \
                 -DNPC_RAM_SIZE=$(NPC_RAM_SIZE)UL \
                 -DNPC_UART_BASE=$(NPC_UART_BASE)UL \
                 -DNPC_RTC_BASE=$(NPC_RTC_BASE)UL

# NPC 自身的启动代码和 AM 接口实现。
AM_SRCS := riscv/npc/start.S \
           riscv/npc/trm.c \
           riscv/npc/ioe.c \
           riscv/npc/timer.c \
           riscv/npc/input.c \
           riscv/npc/cte.c \
           riscv/npc/trap.S \
           platform/dummy/vme.c \
           platform/dummy/mpe.c

# 编译和链接设置
# `-fdata-sections`/`-ffunction-sections`: 每个函数/全局或静态变量单独放一个section
CFLAGS    += -fdata-sections -ffunction-sections
# 将平台地址作为 C 预处理宏传入。
CFLAGS    += $(NPC_CPPFLAGS)
# 公共脚本负责段布局，并根据下面提供的符号检查入口和内存容量。
LDSCRIPTS += $(AM_HOME)/scripts/linker.ld
# 平台符号放在 -T 链接脚本之前，使脚本中的 DEFINED() 能识别它们。
# _pmem_start + _entry_offset 决定程序地址，另两个符号用于布局检查。
LDFLAGS   := --defsym=_pmem_start=$(NPC_RAM_BASE) --defsym=_entry_offset=0 \
             --defsym=_pmem_end=$(NPC_RAM_BASE)+$(NPC_RAM_SIZE) \
             --defsym=_expected_entry=$(NPC_RAM_BASE) $(LDFLAGS)
# `--gc-sections`: 链接时丢弃未使用的section.
LDFLAGS   += --gc-sections -e _start


# 平台配置文件变化后，重编译并重链接。
$(OBJS): $(NPC_CONFIG_MK)
$(IMAGE).elf: $(NPC_CONFIG_MK)

#
MAINARGS_MAX_LEN = 64
MAINARGS_PLACEHOLDER = the_insert-arg_rule_in_Makefile_will_insert_mainargs_here
CFLAGS += -DMAINARGS_MAX_LEN=$(MAINARGS_MAX_LEN) -DMAINARGS_PLACEHOLDER=$(MAINARGS_PLACEHOLDER)

# 给客户程序main函数提供mainargs. 其中变量`mainargs`由用户命令行给出. (注意命令行给出的参数在子make进程中作为环境变量继承.) 使用insert-arg.py脚本, 将mainargs字符串插入到生成的二进制镜像文件中指定位置.
insert-arg: image
	@python $(AM_HOME)/tools/insert-arg.py $(IMAGE).bin $(MAINARGS_MAX_LEN) $(MAINARGS_PLACEHOLDER) "$(mainargs)"

image: image-dep
	@$(OBJDUMP) -d $(IMAGE).elf > $(IMAGE).txt
	@echo + OBJCOPY "->" $(IMAGE_REL).bin
	@$(OBJCOPY) -S --set-section-flags .bss=alloc,contents -O binary $(IMAGE).elf $(IMAGE).bin

# run目标: 进入npc目录, 构建硬件verilator仿真器.
# 具体地: 开个新make进程运行 `npc/Makefile` 的run规则, 并传入生成的bin. 前者将编译出可运行的verilator仿真程序, 并把客户程序image加载进去, 然后运行. 同时传递 ELF 文件路径用于 ftrace
run: insert-arg
	$(MAKE) -C $(NPC_HOME) run IMG=$(IMAGE).bin ELF=$(IMAGE).elf \
		NPC_CPPFLAGS="$(NPC_CPPFLAGS)" NPC_CONFIG_MK="$(NPC_CONFIG_MK)"

# 只构建仿真器，也从此处提供相同的配置。
sim:
	$(MAKE) -C $(NPC_HOME) $@ \
		NPC_CPPFLAGS="$(NPC_CPPFLAGS)" NPC_CONFIG_MK="$(NPC_CONFIG_MK)"

.PHONY: insert-arg sim
