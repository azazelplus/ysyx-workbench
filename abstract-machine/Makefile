# abstract-machine的主目录makefile.  Makefile for AbstractMachine Kernels and Libraries.
# AM的主makefile, 用来被下面三种情况调用.
# AM内核的Makefile  $(AM_HOME)/am/Makefile 内include它, 来构建am内核.
# kilb库的Makefile($(AM_HOME)/klib/Makefile) include它, 来构建klib库.
# cpu-tests的Makefile include它, 来构建测试程序并启动nemu运行.
# am/kernels下的更复杂的测试程序

##########################################################
# 运行本makefile需要提供参数:  

# MAKECMDGOALS
# 这个是所有makefile普遍的没什么好说的. 

# SRCS
# 要编译的源码.
# 在内核Makefile中(am/Makefile)定义为 `SRCS = $(addprefix src/, $(AM_SRCS))`
# 在库Makefile中(klib/Makefile)定义为 `SRCS = $(addprefix src/, $(KLIB_SRCS))`
# 在cpu测试中被定义为对应测试文件 `SRCS = tests/$(ALL).c`, 比如 `dummy.c`

# ARCH
# 要用的架构. 本am根据此来include对应的架构mk. 会被本makefile解析为ISA和PLATFORM变量.

# NAME
# 当前正在编译的模块/库/程序的名称. 它由是谁调用了本makefile决定:
# 被内核 	`am/Makefile`调用时, 			定义为`NAME := am`; 
# 被库 		`klib/Makefile`调用时, 			定义为`NAME = klib`; 
# 被cpu测试	`cpu-tests/Makefile`调用时, 	定义为`NAME = dummy`.

# LIBS
# 程序需要引入的库. 如果是cpu-tests中的简单测试, 它们会传入空.
##########################################################


## 本makefile的调用层级:
# 如果MAKECMDGOALS是run, 本makefile的run规则在架构.mk中定义.
# 如果ARCH=xxx-npc, 	则run定义在 `/scripts/platform/npc.mk` 中, 会执行`make -C $(NPC_HOME) run IMG=$(IMAGE).bin`, 开一个子进程运行npc的Makefile的run规则, 传入IMG参数(本Makefile编译好的二进制文件).
# 如果ARCH=xxx-nemu, 	则run定义在 `/scripts/platform/nemu.mk` 中, 会执行`make -C $(NEMU_HOME) run IMG=$(IMAGE).bin`, 开一个子进程运行nemu的Makefile的run规则, 传入IMG参数(本Makefile编译好的二进制文件).




# 以am-kernels/tests/dummy.c为例, 
# 当在am-kernel目录下输入`make ARCH=riscv32-nemu ALL=dummy run` 时, 调用层级如下:
# tips: makefile的调用分为两种方式: 1.[including]  2.[make -f 指定makefile]

# am-kernels/tests/cpu-tests/Makefile(起始点是tests目录下的Makefile)
#  └── [生成并运行] Makefile.dummy(生成选定测试程序的临时Makefile)
#             └── [include] ${AM_HOME}/Makefile(本Makefile)
#                           └── [include] scripts/riscv32-nemu.mk()
#                                 			├── [include] scripts/isa/riscv.mk(交叉编译器配置)
#                                 			└── [include] scripts/platform/nemu.mk(启动 NEMU)
#                                 							└── [运行] $(NEMU_HOME)/Makefile
#                                        								└──...(nemu编译层级.)










# html规则, 生成个更readable的版本. 使用方式: `make html`. 
# sed -E `s/old/new/g` 是sed替换命令. -E是ERE. 将old(正则表达式)替换为new(替换字符串). g表示全局.
# 此处old=^([^#])
# 即匹配所有行首为非#的(即非注释)的行, 把它们前面加个空格, 然后写到文件Makefile.html中.
html:
	cat Makefile | sed -E 's/^([^#])/    \1/g' | markdown_py > Makefile.html
.PHONY: html



## 1. Basic Setup and Checks

# 当用户没有指定MAKECMDGOALS时, 默认目标是`image`.
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

### Check: environment variable `$ARCH` must be in the supported list. 如果没有指定ARCH会在此报错
ARCHS = $(basename $(notdir $(shell ls $(AM_HOME)/scripts/*.mk)))
ifeq ($(filter $(ARCHS), $(ARCH)), )
  $(error Expected $$ARCH in {$(ARCHS)}, Got "$(ARCH)")
endif

# 从ARCH中拆得ISA和PLATFORM变量. Example: `ARCH=x86_64-qemu -> ISA=x86_64; PLATFORM=qemu`
ARCH_SPLIT = $(subst -, ,$(ARCH))
ISA        = $(word 1,$(ARCH_SPLIT))
PLATFORM   = $(word 2,$(ARCH_SPLIT))

### Check if there is something to build
# flavor函数判断变量类型: undefined(没定义), recurisive(用=定义), simple(用:=定义)
ifeq ($(flavor SRCS), undefined)
  $(error Variable SRCS not defined!! Nothing to build...)
endif

### Checks end here
endif


## 2. 创建输出目录, 里面放所有输出产物. 还有设置输出变量名字

### Create the destination directory DST_DIR, 即 `build/$ARCH`
WORK_DIR  = $(shell pwd)
DST_DIR   = $(WORK_DIR)/build/$(ARCH)
$(shell mkdir -p $(DST_DIR))

### Compilation targets (a binary image or archive)
# img文件相对路径
IMAGE_REL = build/$(NAME)-$(ARCH)
# img文件绝对路径
IMAGE     = $(abspath $(IMAGE_REL))
# .a文件绝对路径
ARCHIVE   = $(WORK_DIR)/build/$(NAME)-$(ARCH).a

### Collect the files to be linked: object files (`.o`) and libraries (`.a`) 

#从SRCS变量生成OBJS变量. 
# 具体, 对SRCS中变量, 1.去掉后缀; 2.加.o后缀; 3.加上路径前缀$(DST_DIR)/
OBJS      = $(addprefix $(DST_DIR)/, $(addsuffix .o, $(basename $(SRCS))))

# LIBS: 程序依赖的库. 这一句当前将LIBS变量用sort去重, 然后强制加上am和klib. 如果在运行cpu-tests下的测试, 它们都不需要额外添加库. 如果是am/kernel下的程序, 它们会传入额外需要的库到LIBS.
LIBS     := $(sort $(LIBS) am klib) # lazy evaluation ("=") causes infinite recursions

# LINKAGE: 这是一个文件路径列表，保存了所有需要交给链接器(Linked, ld)去合并的二进制文件路径. 此处先包括所有OBJ, 后面会添加更多库文件路径.
LINKAGE   = $(OBJS)  # library archives are added by LIB_TEMPLATE below




## 3. General Compilation Flags编译命令选项变量.

### 交叉编译选项(Cross) compilers, e.g., mips-linux-gnu-g++
# CROSS_COMPILE变量来自ARCH变量include的mk文件(见下文4.)
# 例如, 在riscv.mk中被定义为 `riscv64-linux-gnu-`
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

# ARCH_H: 指定当前架构对应的arch头文件路径.
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



## 4.引入所选择ARCH的makefile.
# 例如, 如果ARCH=riscv32-nemu, 那么此处include `$(AM_HOME)/scripts/riscv32-nemu.mk`
# `riscv32-nemu.mk`内又会include对应ISA和PLATFORM对应makefile: `scripts/isa/riscv.mk`和`scripts/platform/nemu.mk`.
-include $(AM_HOME)/scripts/$(ARCH).mk




## 5. 编译链接工作. Compilation Rules 编译OBJ, 链接image的编译器命令.

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
