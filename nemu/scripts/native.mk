#***************************************************************************************
# 提供run规则, 运行NEMU可执行程序. 被build.mk include.
#**************************************************************************************/

-include $(NEMU_HOME)/../Makefile
include $(NEMU_HOME)/scripts/build.mk

include $(NEMU_HOME)/tools/difftest.mk

#函数git_commit在ysyx-workbench/Makefile中定义. ysyx注入的监视器. 它说别动这个.
compile_git:
	$(call git_commit, "compile NEMU")
$(BINARY):: compile_git

# Some convenient rules

override ARGS ?= --log=$(BUILD_DIR)/nemu-log.txt
override ARGS += $(ARGS_DIFF)

# Command to execute NEMU
IMG ?=
NEMU_EXEC := $(BINARY) $(ARGS) $(IMG)

# make run的依赖. 包括
run-env: $(BINARY) $(DIFF_REF_SO)

# 主规则run. 运行NEMU模拟器. 
run: run-env
# 这个是和nemu本身无关的行为. git_commit是上一级ysyx的监视器.
	$(call git_commit, "run NEMU")
# 这是 make run 的总命令: /build/riscv32-nemu-interpreter --log=/build/nemu-log.txt [ARGS_DIFF内容] [IMG文件]
	$(NEMU_EXEC)

gdb: run-env
	$(call git_commit, "gdb NEMU")
	gdb -s $(BINARY) --args $(NEMU_EXEC)

clean-tools = $(dir $(shell find ./tools -maxdepth 2 -mindepth 2 -name "Makefile"))
$(clean-tools):
	-@$(MAKE) -s -C $@ clean
clean-tools: $(clean-tools)
clean-all: clean distclean clean-tools

.PHONY: run gdb run-env clean-tools clean-all $(clean-tools)
