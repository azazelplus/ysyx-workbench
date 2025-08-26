#本Makefile可以实现:
# make build自动识别当前目录的.v和.cpp文件链接编译.  
# make sim: 执行make buiuld的内容, 并运行得到的可执行文件V$(TOP_MODULE). 注意,请给出唯一的.v和.cpp文件在当前目录! 否则,请手动指定VERILOG_SR和CPP_SR, 例如:
# make sim VERILOG_SRC=mymodule.v CPP_SRC=mysim.cpp 

#利用wildcard来自动检测当前目录的目标.v文件和目标.cpp文件.
VERILOG_SRC := $(wildcard *.v)
CPP_SRC := $(wildcard *.cpp)

#动态获取.v文件的顶层模块名, 和生成的可执行文件名.
TOP_MODULE := $(basename $(VERILOG_SRC))
EXECUTABLE := obj_dir/V$(TOP_MODULE)


all:
	@echo "Write this Makefile by your self."

#sim是Makefile的一个目标(TARGET). Makefile就是一个个TARGET组成的. 输入make sim, make就会找到sim:这个目标, 然后执行它的命令. sim:build表示`sim`目标依赖于`build`目标, 会先执行build, 然后来执行sim.
#`@`的作用是 不在终端打印这条命令本身，但仍然会执行它.
sim:build	
	$(call git_commit, "sim RTL") # DO NOT REMOVE THIS LINE!!! 这行代码在每次运行`make sim`后自动执行一次, 在本地仓库执行一次提交.
	@echo "azazel's running simulation..."
	@$(EXECUTABLE)		#运行build好的可执行文件V$(TOP_MODULE).
	
build:
	verilator --cc --exe --build -j 0 -Wall --trace $(CPP_SRC) $(VERILOG_SRC)
	@echo "azazel's build completed!"




#把上一级那个Makefile直接插入过来了. 会利用其上的内容.
include /home/azazel/ysyx-workbench/Makefile
