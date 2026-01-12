#本Makefile可以实现:
# make build自动识别当前目录的.v和.cpp文件链接编译.  
# make sim: 执行make buiuld的内容, 并运行得到的可执行文件V$(TOP_MODULE). 注意,请给出唯一的.v和.cpp文件在当前目录! 否则,请手动指定VERILOG_SR和CPP_SR, 例如:
# make sim VERILOG_SRC=mymodule.v CPP_SRC=mysim.cpp 

# VERILOG_SRC: 当前目录的.v文件
# CPP_SRC: 当前目录的.cpp文件.
VERILOG_SRC := $(wildcard *.v)
CPP_SRC := $(wildcard *.cpp)

#动态获取.v文件的顶层模块名, 和生成的可执行文件名.
TOP_MODULE := $(basename $(VERILOG_SRC))
EXECUTABLE := obj_dir/V$(TOP_MODULE)


all:
	@echo "Write this Makefile by your self."

#sim目标: 依赖于build目标. 运行模拟.
sim:build	
	$(call git_commit, "sim RTL") # DO NOT REMOVE THIS LINE!!! 这行代码在每次运行`make sim`后自动执行一次, 在本地仓库执行一次提交.
	@echo "azazel's running simulation..."
	@$(EXECUTABLE)		#运行build好的可执行文件V$(TOP_MODULE).
	
# 构建目标. 
build:
	verilator --cc --exe --build -j 0 -Wall --trace $(CPP_SRC) $(VERILOG_SRC)
	@echo "azazel's build completed!"




# ysyx的主Makefile.
include /home/azazel/ysyx-workbench/Makefile
