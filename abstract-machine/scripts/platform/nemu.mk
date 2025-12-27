# 本makefile被 riscv32-nemu.mk 包含. 用来启动nemu. 它将会执行bash命令:
# /home/azazel/ysyx-workbench/nemu/build/riscv32-nemu-interpreter \
    -b \
    -l /path/to/nemu-log.txt \
    /path/to/dummy-riscv32-nemu.bin

# 从而启动riscv32-nemu-interpreter程序. 上述参数-b, -l, /path...被传入nemu的main函数nemu-main.c的函数: int main(int argc, char *argv[]) , 再调用子函数 `init_monitor(argc, argv);` , 再调用子函数parse_args()函数解析处理.

# 本makefile调用层级:
# $(AM_HOME)/Makefile(AM主makefile) 
#   └──include $(AM_HOME)/scripts/riscv32-nemu.mk 
#        └──include $(AM_HOME)/scripts/platform/nemu.mk (本makefile)
#               └── 运行 $(NEMU_HOME)/Makefile
#                           └── ...(nemu编译层级..)


AM_SRCS := platform/nemu/trm.c \
           platform/nemu/ioe/ioe.c \
           platform/nemu/ioe/timer.c \
           platform/nemu/ioe/input.c \
           platform/nemu/ioe/gpu.c \
           platform/nemu/ioe/audio.c \
           platform/nemu/ioe/disk.c \
           platform/nemu/mpe.c

CFLAGS    += -fdata-sections -ffunction-sections
# NEMUFLAGS是运行 `$(NEMU_HOME)/Makefile` 的make run时, 要传给`$(NEMU_HOME)/Makefile` 内变量ARGS的参数. 见下方命令 `$(MAKE) ... ARGS="$(NEMUFLAGS)" ...`
# 展开就是-l /home/azazel/ysyx-workbench/am-kernels/tests/cpu-tests/build/nemu-log.txt
# 其中 IMAGE 定义在$(AM_HOME)/Makefile:   IMAGE     = $(abspath $(IMAGE_REL))
# 其中  IMAGE_REL = build/$(NAME)-$(ARCH)
# 其中 ARCH = riscv32-nemu; NAME由临时makefile传入, 即测试文件.c词干.
# -l是nemu的命令行参数, 指定日志文件路径.
# 这样 NEMU 在运行时会把日志输出到编译输出目录下的 nemu-log.txt 文件.

# -b是nemu的批处理模式参数, 启动后直接运行程序, 不进入交互式sdb.
CFLAGS    += -I$(AM_HOME)/am/src/platform/nemu/include
LDSCRIPTS += $(AM_HOME)/scripts/linker.ld
LDFLAGS   += --defsym=_pmem_start=0x80000000 --defsym=_entry_offset=0x0
LDFLAGS   += --gc-sections -e _start
NEMUFLAGS += -b -l $(shell dirname $(IMAGE).elf)/nemu-log.txt

MAINARGS_MAX_LEN = 64
MAINARGS_PLACEHOLDER = the_insert-arg_rule_in_Makefile_will_insert_mainargs_here
CFLAGS += -DMAINARGS_MAX_LEN=$(MAINARGS_MAX_LEN) -DMAINARGS_PLACEHOLDER=$(MAINARGS_PLACEHOLDER)

insert-arg: image
	@python $(AM_HOME)/tools/insert-arg.py $(IMAGE).bin $(MAINARGS_MAX_LEN) $(MAINARGS_PLACEHOLDER) "$(mainargs)"

image: image-dep
	@$(OBJDUMP) -d $(IMAGE).elf > $(IMAGE).txt
	@echo + OBJCOPY "->" $(IMAGE_REL).bin
	@$(OBJCOPY) -S --set-section-flags .bss=alloc,contents -O binary $(IMAGE).elf $(IMAGE).bin

# run伪目标: 调用NEMU运行镜像文件.
# $(MAKE)是make的内置变量, 它的值是当前正在运行的make命令加上行为类选项 (behavior option), 比如-j8(并行编译), -s(静默模式), -k(忽略错误), -l(负载限制).
# 不直接写make, 而是用$(MAKE), 可以让子make进程继承父make进程的行为类选项.
# 其实我感觉完全是脱裤子放屁. 多写个-j8会累死你吗? 你妈的写个$(MAKE)you think you are so cool?
# -C选项即cd, 先切换目录再运行make. 此处切到NEMU_HOME目录了, 此时运行的就是nemu的主Makefile, 会启动nemu. 同时传参数ISA和IMG.
run: insert-arg
	$(MAKE) -C $(NEMU_HOME) ISA=$(ISA) run ARGS="$(NEMUFLAGS)" IMG=$(IMAGE).bin

gdb: insert-arg
	$(MAKE) -C $(NEMU_HOME) ISA=$(ISA) gdb ARGS="$(NEMUFLAGS)" IMG=$(IMAGE).bin

.PHONY: insert-arg
