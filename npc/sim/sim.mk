# npc/sim/sim.mk
# 仿真相关的构建规则. 本makefile调用了verilator,编译出npc的可执行仿真程序.
# 由npc/Makefile include.

# 在这个过程中, (make[n]计数器其实就是环境变量$MAKELEVEL的值)
# 一共有 3 个make进程, 每个进程中在执行的makefile如下:
# make[0]: cpu-test/Makefile
# make[1]: Makefile.[待测试程序](include: [AM_HOME]/Makefile(include: /scripts/platform/npc.mk))
# make[2]: npc/Makefile(include: npc/sim/sim.mk)



$(info )
$(info [MAKE_DEBUG] =================================================)
$(info [MAKE_DEBUG] Current make level $$(MAKELEVEL): $(MAKELEVEL))
$(info [MAKE_DEBUG] Current dir $$(CURDIR):   	$(CURDIR))
$(info [MAKE_DEBUG] $$(MAKEFILE_LIST): 		$(MAKEFILE_LIST))
$(info [MAKE_DEBUG] Current makefile:	$(abspath $(lastword $(MAKEFILE_LIST))))
$(info [MAKE_DEBUG] =================================================)
$(info )



# 目录定义 (基于项目根目录)
BUILD_DIR ?= build
GEN_DIR   ?= generated
SIM_DIR   ?= sim
SRC_DIR   ?= $(SIM_DIR)/src
OBJ_DIR   := $(BUILD_DIR)/obj_dir

# 仿真可执行文件路径: build/obj_dir/VAzazeRV
SIM_BIN   := $(OBJ_DIR)/VAzazeRV

# NPC_CPPFLAGS 和 NPC_CONFIG_MK 由 scripts/platform/npc.mk 传入。

# ================= LLVM 配置 (用于反汇编器) =================
# 过滤掉 -D__STDC_FORMAT_MACROS 以避免与 Verilator 的头文件冲突
LLVM_CXXFLAGS := $(filter-out -D__STDC_FORMAT_MACROS, $(shell llvm-config --cxxflags)) -fPIE
LLVM_LIBS     := $(shell llvm-config --libs)

# Verilator 编译选项
VERILATOR_FLAGS += --cc --exe --build --trace 
VERILATOR_FLAGS += -Wall -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM
VERILATOR_FLAGS += --top-module AzazeRV 
VERILATOR_FLAGS += --Mdir $(OBJ_DIR)
VERILATOR_FLAGS += -I$(GEN_DIR)
# 使用绝对路径以避免在 obj_dir 下找不到头文件（如 config.h）
VERILATOR_FLAGS += -I$(abspath $(SRC_DIR))
VERILATOR_FLAGS += -I$(abspath $(SRC_DIR)/trace)
# 链接 dl(dynamic linker, 动态链接) 库 (因为DiffTest需要库函数dlopen/dlsym).  -ldl: -l是linux链接器选项, dl是库名, 即libdl.so. 这个库中有函数 dlopen, dlsym... `-LDFLAGS`是verilator的选项, 用来传递给最终链接器gcc的选项.
# 添加 LLVM 库链接，抑制 __STDC_FORMAT_MACROS 重定义警告（来自 LLVM cxxflags）
VERILATOR_FLAGS += -LDFLAGS "-ldl -lreadline $(LLVM_LIBS)"


# trace 表达式 编译选项.
# CONFIG_xTRACE_COND_EXPR 默认为 true (始终输出 dtrace), 可通过命令行覆盖: make run DTRACE_COND="(g_cycle > 1000)" 来限制输出范围. 例如:
# 仅追踪10000周期后的指令: make sim ITRACE_COND="g_cycle > 10000" (判断 opcode 是否为 store 指令 (S-type: 0100011 = 0x23).)
# 仅追踪特定指令(例如所有写内存指令): make sim ITRACE_COND="(inst & 0x7f) == 0x23"

# DTRACE 编译选项
DTRACE_FLAGS += -DCONFIG_DTRACE_COND_EXPR=$(if $(DTRACE_COND),$(DTRACE_COND),true)
# ITRACE 编译选项
ITRACE_FLAGS += -DCONFIG_ITRACE_COND_EXPR=$(if $(ITRACE_COND),$(ITRACE_COND),true)
# MTRACE 编译选项
MTRACE_FLAGS += -DCONFIG_MTRACE_COND_EXPR=$(if $(MTRACE_COND),$(MTRACE_COND),true)
# FTRACE 编译选项
FTRACE_FLAGS += -DCONFIG_FTRACE_COND_EXPR=$(if $(FTRACE_COND),$(FTRACE_COND),true)
# 汇总一下得到TRACE_FLAGS, 注入VERILATOR_FLAGS.
TRACE_FLAGS += $(DTRACE_FLAGS) $(ITRACE_FLAGS) $(MTRACE_FLAGS) $(FTRACE_FLAGS)

# C++ 编译标志 (传递给 Verilator 的 C++ 编译器)
# 需要在 CFLAGS 中再携带包含路径，否则生成的 VAzazeRV.mk 调用 g++ 时找不到 config.h
# 添加 LLVM 编译选项 (用于 disasm.cpp)
VERILATOR_FLAGS += -CFLAGS "-I$(abspath $(SRC_DIR)) -I$(abspath $(SRC_DIR)/trace) $(NPC_CPPFLAGS) $(TRACE_FLAGS) $(LLVM_CXXFLAGS)"



# 参与仿真编译的C++ 源文件: sim/src下所有.c .cpp .cc文件. 
# 使用绝对路径，以便 Verilator 在 obj_dir 下执行时能正确找到
SIM_CSRCS := $(shell find $(abspath $(SRC_DIR)) \
		-name "*.c" \
	 -o -name "*.cpp" \
	 -o -name "*.cc"\
)

# 参与仿真编译的头文件: sim/src下所有.h .hpp .hh文件.
# 使用绝对路径，以便 Verilator 在 obj_dir 下执行时能正确找到
SIM_HEADERS := $(shell find $(abspath $(SRC_DIR)) \
		-name "*.h" \
	 -o -name "*.hpp" \
	 -o -name "*.hh"\
)


# 所有 ExtModule 资源文件均通过 addResource() 由 Chisel 复制到 generated/, 无需单独列出.
# EXT_SV_SRCS := $(shell find src/main/resources -name "*.sv") # 找到所有存在`src/main/resources`里的External SV files (ExtModule 的外部实现, 如 DPI-C 模块)

# Scala 源文件 (用于检测更新)
SCALA_SRCS := $(shell find src -name "*.scala")

# ================= 目标定义 =================

.PHONY: verilog sim clean

# 1. 依赖scala代码, 生成 Verilog代码
verilog: $(GEN_DIR)/AzazeRV.sv

$(GEN_DIR)/AzazeRV.sv: $(SCALA_SRCS)
	@echo "=== [1/3] Generating Verilog (SBT) ==="
# 执行sbt命令来编译scala源码以生成verilog. runMain是sbt构建工具提供的标准任务.
	sbt 'runMain azazerv.AzazeRV'	


# 2. sim规则: 依赖verilog代码核sim/src的C代码, 使用verilator命令, 编译仿真器 (最后编译出可执行文件`VAzazeRV`)
# 依赖生成的 Verilog、C++ 源码和头文件, 生成SIM_BIN, 可执行的仿真文件.
sim: $(SIM_BIN)

.PHONY: check-config
check-config:
	@test -n "$(NPC_CPPFLAGS)" || { \
		echo 'Missing NPC_CPPFLAGS. Build through AM: make -C ../abstract-machine/am ARCH=riscv32-npc sim'; \
		exit 1; \
	}

# 不依赖 Verilator 的内存模块检查。
.PHONY: check-memory
check-memory: $(BUILD_DIR)/tests/memory-test
	$<

$(BUILD_DIR)/tests/memory-test: $(SRC_DIR)/memory.cpp $(SRC_DIR)/memory.h sim/tests/memory_test.cpp $(NPC_CONFIG_MK) | check-config
	@mkdir -p $(dir $@)
	$(CXX) -std=c++14 -Wall -Wextra -Werror -I$(SRC_DIR) $(NPC_CPPFLAGS) \
		$(SRC_DIR)/memory.cpp sim/tests/memory_test.cpp -o $@

# 参与编译的verilog文件:
$(SIM_BIN): $(GEN_DIR)/AzazeRV.sv $(SIM_CSRCS) $(SIM_HEADERS) $(NPC_CONFIG_MK) $(abspath sim/sim.mk) | check-config
	@echo "=== [2/3] Compiling Simulation (Verilator) ==="
	@mkdir -p $(OBJ_DIR)
# AM 平台配置变化时，让 Verilator 的内层 make 也应用新的 -D 选项。
	verilator $(VERILATOR_FLAGS) \
		$(if $(filter $(NPC_CONFIG_MK),$?),-MAKEFLAGS "-B") \
		$(SIM_CSRCS) \
		$(GEN_DIR)/*.sv \
		-o $(abspath $(SIM_BIN))

# 3. 运行仿真
# 需指定 IMG=xxx.bin
# 变量`ELF`来自am的编译构建过程, 由npc.mk的run提供. 测试程序的ELF路径, 用于ftrace.
run: $(SIM_BIN)
ifndef IMG
	$(error	Error: IMG variable is not set. Usage: make run IMG=prog.bin [ELF=prog.elf])
endif
	@echo "=== [3/3] Running Simulation ==="
# `$(if $(ELF),--elf=$(ELF))` 如果提供了 ELF 文件，那么 就在命令行加上 --elf=文件名；否则，这里什么都不写.”
# `$(if $(MAX_CYCLES),--cycles=$(MAX_CYCLES))` 如果 用户指定了最大周期，那么 加上 --cycles=数值；否则，这里也是空白。”
	$(SIM_BIN) $(IMG) $(if $(ELF),--elf=$(ELF)) $(if $(MAX_CYCLES),--cycles=$(MAX_CYCLES))

# 清理
clean:
	rm -rf $(BUILD_DIR) $(GEN_DIR)
