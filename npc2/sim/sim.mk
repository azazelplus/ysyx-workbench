# npc2/sim/sim.mk
# 仿真相关的构建规则

# 目录定义 (基于项目根目录)
BUILD_DIR ?= build
GEN_DIR   ?= generated
SIM_DIR   ?= sim
OBJ_DIR   := $(BUILD_DIR)/obj_dir

# 仿真可执行文件路径
SIM_BIN   := $(OBJ_DIR)/VMiniRV

# Verilator 编译选项
VERILATOR_FLAGS += --cc --exe --build --trace 
VERILATOR_FLAGS += -Wall -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM
VERILATOR_FLAGS += --top-module MiniRV 
VERILATOR_FLAGS += --Mdir $(OBJ_DIR)
VERILATOR_FLAGS += -I$(GEN_DIR)

# 参与仿真编译的C++ 源文件:
# 
SIM_CSRCS := $(abspath \
	$(SIM_DIR)/main.cpp \
	$(SIM_DIR)/disasm.cpp \
)

# Scala 源文件 (用于检测更新)
SCALA_SRCS := $(shell find src -name "*.scala")


# 默认仿真周期
MAX_CYCLES ?= 100000

# ================= 目标定义 =================

.PHONY: verilog sim clean

# 1. 生成 Verilog
verilog: $(GEN_DIR)/MiniRV.sv

$(GEN_DIR)/MiniRV.sv: $(SCALA_SRCS)
	@echo "=== [1/3] Generating Verilog (Mill) ==="
# 执行mill命令来编译scala源码以生成verilog. runMain是mill构建工具提供的标准任务.
	./mill npc2.runMain minirv.MiniRV	


# 2. 编译仿真器 (使用Verilator)
# 依赖生成的 Verilog 和 C++ 源码, 生成SIM_BIN, 可执行的仿真文件.
sim: $(SIM_BIN)

$(SIM_BIN): $(GEN_DIR)/MiniRV.sv $(SIM_CSRCS)
	@echo "=== [2/3] Compiling Simulation (Verilator) ==="
	@mkdir -p $(OBJ_DIR)
	verilator $(VERILATOR_FLAGS) \
		$(SIM_CSRCS) \
		$(GEN_DIR)/*.sv \
		-o $(abspath $(SIM_BIN))

# 3. 运行仿真
# 需指定 IMG=xxx.bin
run: $(SIM_BIN)
ifndef IMG
	$(error Error: IMG variable is not set. Usage: make run IMG=prog.bin)
endif
	@echo "=== [3/3] Running Simulation ==="
	$(SIM_BIN) $(IMG) $(MAX_CYCLES)

# 清理
clean:
	rm -rf $(BUILD_DIR) $(GEN_DIR)
