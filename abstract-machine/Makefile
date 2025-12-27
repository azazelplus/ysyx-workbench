# Makefile for AbstractMachine Kernels and Libraries

# 这是AM主Makefile. 
# AM内核的Makefile($(AM_HOME)/am/Makefile) include它, 来构建am内核.
# kilb库的Makefile($(AM_HOME)/klib/Makefile) include它, 来构建klib库.
# cpu-tests的Makefile include它, 来构建测试程序并启动nemu运行.




# 整个使用AM编译程序在nemu上运行(以am-kernels/tests/dummy.c为例, 此时我们在am-kernel目录下输入`make ARCH=riscv32-nemu ALL=dummy run`)的调用层级如下:
# makefile的调用分为两种方式: 1.[including]  2.[make -f 指定makefile]

# am-kernels/tests/cpu-tests/Makefile(起始点是tests目录下的Makefile)
#  └── [生成并运行] Makefile.dummy(生成选定测试程序的临时Makefile)
#             └── [include] ${AM_HOME}/Makefile(本AM主Makefile)
#                           └── [include] scripts/riscv32-nemu.mk
#                                 			├── [include] scripts/isa/riscv.mk(交叉编译器配置)
#                                 			└── [include] scripts/platform/nemu.mk(启动 NEMU)
#                                 							└── [运行] $(NEMU_HOME)/Makefile
#                                        								└──...(nemu编译层级.)

##########################################################
# 主要变量:
# ARGS
# 来源: 本Makefile认为由上层提供. 实际上是
# 用途: 在最后 `[运行] $(NEMU_HOME)/Makefile` 时, 给可执行文件 `riscv32-nemu-interpreter` 传递的命令行参数.

# NEMUFLAGS: 在nemu.mk定义. 是运行 `$(NEMU_HOME)/Makefile` 的make run时, 要传给`$(NEMU_HOME)/Makefile` 内变量ARGS的参数.

# ARCH
# 来源: 本Makefile认为由上层提供. 实际上要命令行run时指定.
# 用途: 指定编译的 `isa-platform`. `riscv32-nemu` 即 `isa=riscv32; platform=nemu`.

# NAME
# 来源: 本Makefile认为由上层提供.
# 被am/Makefile调用时, 定义为`NAME := am`; 
# 被klib/Makefile调用时, 定义为`NAME = klib`; 
# 被cpu-test的某个测试dummy的Makefile调用时, 定义为`NAME = dummy`.
# 用途: NAME是当前正在编译的模块/库/程序的名称，用于生成输出文件名.

#
##########################################################




# $(NEMU_HOME)/Makefile(即nemu主Makefile) 编译构建的层级:(它最终编译出riscv32-nemu-interpreter可执行文件)

# $(NEMU_HOME)/Makefile(nemu主Makefile)
#   ├── [include] $(NEMU_HOME)/include/config/auto.conf          [配置变量]  
#   ├── [include] $(NEMU_HOME)/include/config/auto.conf.cmd      [配置命令]
#   ├── [include] $(NEMU_HOME)/src/filelist.mk                   [顶层源文件列表]
#   │             └──(被find命令找到的所有子目录的 filelist.mk)
#   │                       ├── [include] $(NEMU_HOME)/src/engine/filelist.mk    [引擎模块源文件]
#   │                       ├── [include] $(NEMU_HOME)/src/device/filelist.mk    [设备模块源文件]
#   │                       ├── [include] $(NEMU_HOME)/src/isa/filelist.mk       [ISA架构源文件]
#   │                       └── [include] $(NEMU_HOME)/src/utils/filelist.mk     [工具模块源文件]
#   ├── [include] $(NEMU_HOME)/scripts/config.mk                 [Kconfig 配置规则]
#   └── [include] $(NEMU_HOME)/scripts/native.mk                 [本地构建规则]
#                 ├── [include] $(NEMU_HOME)/scripts/build.mk              [核心编译规则]
#                 └── [include] $(NEMU_HOME)/tools/difftest.mk             [差分测试规则]


# 









### *Get a more readable version of this Makefile* by `make html` (requires python-markdown)
# 使用方式: `make html`. 
# sed -E `s/old/new/g` 是sed替换命令. -E是ERE. 将old(正则表达式)替换为new(替换字符串). g表示全局.
# 此处old=^([^#])
# 即匹配所有行首为非#的(即非注释)的行, 把它们前面加个空格, 然后写到文件Makefile.html中.
html:
	cat Makefile | sed -E 's/^([^#])/    \1/g' | markdown_py > Makefile.html
.PHONY: html



## 1. Basic Setup and Checks

### Default to create a bare-metal kernel image
# 当用户没有在命令行指定make目标时, 默认目标是`image`.
ifeq ($(MAKECMDGOALS),)
  MAKECMDGOALS  = image
  .DEFAULT_GOAL = image
endif

### Override checks when `make clean/clean-all/html`
# findstring <find>,<in>用法: 如果 <find> 是 <in> 的子串则返回 <find>，否则返回空字符串. 
# 如果当前目标是 clean/clean-all/html, 则跳过下面的检查.
ifeq ($(findstring $(MAKECMDGOALS),clean|clean-all|html),)

### Print build info message
# info打印信息到stdout.
# 其中NAME在am-kernels/am/Makefile中定义为`NAME :=am`
$(info # Building $(NAME)-$(MAKECMDGOALS) [$(ARCH)])

### Check: environment variable `$AM_HOME` looks sane
ifeq ($(wildcard $(AM_HOME)/am/include/am.h),)
  $(error $$AM_HOME must be an AbstractMachine repo)
endif

### Check: environment variable `$ARCH` must be in the supported list
ARCHS = $(basename $(notdir $(shell ls $(AM_HOME)/scripts/*.mk)))
ifeq ($(filter $(ARCHS), $(ARCH)), )
  $(error Expected $$ARCH in {$(ARCHS)}, Got "$(ARCH)")
endif

### Extract instruction set architecture (`ISA`) and platform from `$ARCH`. Example: `ARCH=x86_64-qemu -> ISA=x86_64; PLATFORM=qemu`
ARCH_SPLIT = $(subst -, ,$(ARCH))
ISA        = $(word 1,$(ARCH_SPLIT))
PLATFORM   = $(word 2,$(ARCH_SPLIT))

### Check if there is something to build
ifeq ($(flavor SRCS), undefined)
  $(error Nothing to build)
endif

### Checks end here
endif

## 2. General Compilation Targets

### Create the destination directory (`build/$ARCH`)
WORK_DIR  = $(shell pwd)
DST_DIR   = $(WORK_DIR)/build/$(ARCH)
$(shell mkdir -p $(DST_DIR))

### Compilation targets (a binary image or archive)
IMAGE_REL = build/$(NAME)-$(ARCH)
IMAGE     = $(abspath $(IMAGE_REL))
ARCHIVE   = $(WORK_DIR)/build/$(NAME)-$(ARCH).a

### Collect the files to be linked: object files (`.o`) and libraries (`.a`)
OBJS      = $(addprefix $(DST_DIR)/, $(addsuffix .o, $(basename $(SRCS))))
# 将LIBS变量用sort去重, 然后加上am和klib这两个目标.
LIBS     := $(sort $(LIBS) am klib) # lazy evaluation ("=") causes infinite recursions
LINKAGE   = $(OBJS)  # library archives are added by LIB_TEMPLATE below

## 3. General Compilation Flags

### (Cross) compilers, e.g., mips-linux-gnu-g++
# CROSS_COMPILE变量在riscv.mk中被定义为 `riscv64-linux-gnu-`
AS        = $(CROSS_COMPILE)gcc
CC        = $(CROSS_COMPILE)gcc
CXX       = $(CROSS_COMPILE)g++
LD        = $(CROSS_COMPILE)ld
AR        = $(CROSS_COMPILE)ar
OBJDUMP   = $(CROSS_COMPILE)objdump
OBJCOPY   = $(CROSS_COMPILE)objcopy
READELF   = $(CROSS_COMPILE)readelf

### Compilation flags
INC_PATH += $(WORK_DIR)/include $(addsuffix /include/, $(addprefix $(AM_HOME)/, $(LIBS)))
INCFLAGS += $(addprefix -I, $(INC_PATH))

ARCH_H := arch/$(ARCH).h
CFLAGS   += -O2 -MMD -Wall -Werror $(INCFLAGS) \
            -D__ISA__=\"$(ISA)\" -D__ISA_$(shell echo $(ISA) | tr a-z A-Z)__ \
            -D__ARCH__=$(ARCH) -D__ARCH_$(shell echo $(ARCH) | tr a-z A-Z | tr - _) \
            -D__PLATFORM__=$(PLATFORM) -D__PLATFORM_$(shell echo $(PLATFORM) | tr a-z A-Z | tr - _) \
            -DARCH_H=\"$(ARCH_H)\" \
            -fno-asynchronous-unwind-tables -fno-builtin -fno-stack-protector \
            -Wno-main -U_FORTIFY_SOURCE -fvisibility=hidden
CXXFLAGS +=  $(CFLAGS) -ffreestanding -fno-rtti -fno-exceptions
ASFLAGS  += -MMD $(INCFLAGS)
LDFLAGS  += -z noexecstack $(addprefix -T, $(LDSCRIPTS))

## 4. Arch-Specific Configurations

### Paste in arch-specific configurations (e.g., from `scripts/x86_64-qemu.mk`)
# 如果指定指令集, 比如在当前目录make ARCH=riscv32-nemu, 那么此处include ysyx/abstract-machine/scripts/riscv32-nemu.mk.
# `riscv32-nemu.mk`内又会include两个makefile: `scripts/isa/riscv.mk`和`scripts/platform/nemu.mk`.
-include $(AM_HOME)/scripts/$(ARCH).mk

## 5. Compilation Rules

### Rule (compile): a single `.c` -> `.o` (gcc)
$(DST_DIR)/%.o: %.c
	@mkdir -p $(dir $@) && echo + CC $<
	@$(CC) -std=gnu11 $(CFLAGS) -c -o $@ $(realpath $<)

### Rule (compile): a single `.cc` -> `.o` (g++)
$(DST_DIR)/%.o: %.cc
	@mkdir -p $(dir $@) && echo + CXX $<
	@$(CXX) -std=c++17 $(CXXFLAGS) -c -o $@ $(realpath $<)

### Rule (compile): a single `.cpp` -> `.o` (g++)
$(DST_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@) && echo + CXX $<
	@$(CXX) -std=c++17 $(CXXFLAGS) -c -o $@ $(realpath $<)

### Rule (compile): a single `.S` -> `.o` (gcc, which preprocesses and calls as)
$(DST_DIR)/%.o: %.S
	@mkdir -p $(dir $@) && echo + AS $<
	@$(AS) $(ASFLAGS) -c -o $@ $(realpath $<)

ifeq ($(MAKECMDGOALS),archive)
### Rule (archive): objects (`*.o`) -> `ARCHIVE.a` (ar)
$(ARCHIVE): $(OBJS)
	@echo + AR "->" $(shell realpath $@ --relative-to .)
	@$(AR) rcs $@ $^
else
# $(1): library name
define LIB_TEMPLATE =
$$(AM_HOME)/$(1)/build/$(1)-$$(ARCH).a: force
	@$$(MAKE) -s -C $$(AM_HOME)/$(1) archive
LINKAGE += $$(AM_HOME)/$(1)/build/$(1)-$$(ARCH).a
endef

### Rule (recursive make): build a dependent library (am, klib, ...)
# 静态模式规则. 依赖是空的, 类似伪目标.. 目的是调用am/Makefile 和 klib/Makefile的archive目标.
# 这两个Makefile会定义好任务变量(NAME, SRCS, INC_PATH三个变量), 然后include回到本Makefile来完成编译.
# LIBS目标是IMAGE目标的依赖. 当make要构建IMAGE时, 此规则会被触发.
# LIB在本Makefile中被定义为`LIBS := $(sort $(LIBS) am klib)`. 它至少会包含am和klib两个规则.
$(foreach lib, $(LIBS), $(eval $(call LIB_TEMPLATE,$(lib))))
endif

### Rule (link): objects (`*.o`) and libraries (`*.a`) -> `IMAGE.elf`, the final ELF binary to be packed into image (ld)
# 生成最终ELF时(即), make要保证依赖OBJS和LIBS都已经生成好了.
$(IMAGE).elf: $(LINKAGE) $(LDSCRIPTS)
	@echo \# Creating image [$(ARCH)]
	@echo + LD "->" $(IMAGE_REL).elf
ifneq ($(filter $(ARCH),native),)
	@$(CXX) -o $@ -Wl,--whole-archive $(LINKAGE) -Wl,-no-whole-archive $(LDFLAGS_CXX)
else
	@$(LD) $(LDFLAGS) -o $@ --start-group $(LINKAGE) --end-group
endif

### Rule (`#include` dependencies): paste in `.d` files generated by gcc on `-MMD`
# 自动找到所有由gcc编译时生成的.d后缀依赖文件, include进来.
# basename: 去除路径和后缀, 得到词干.
# addsuffix: 给词干加上.d后缀.
# addprefix: 给.d文件加上路径前缀$(DST_DIR)/
-include $(addprefix $(DST_DIR)/, $(addsuffix .d, $(basename $(SRCS))))

## 6. Miscellaneous

### Build order control
image: image-dep
archive: $(ARCHIVE)
image-dep: $(IMAGE).elf
.PHONY: image image-dep archive run

### Force to rebuild a rule
force:
.PHONY: force

### Clean a single project (remove `build/`)
clean:
	rm -rf Makefile.html $(WORK_DIR)/build/
.PHONY: clean

### Clean all sub-projects within depth 2 (and ignore errors)
CLEAN_ALL = $(dir $(shell find . -mindepth 2 -name Makefile))
clean-all: $(CLEAN_ALL) clean
$(CLEAN_ALL):
	-@$(MAKE) -s -C $@ clean
.PHONY: clean-all $(CLEAN_ALL)
