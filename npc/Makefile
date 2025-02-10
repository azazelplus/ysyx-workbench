all:
	@echo "Write this Makefile by your self."

#sim是Makefile的一个目标(TARGET). Makefile就是一个个TARGET组成的. 输入make sim, make就会找到sim:这个目标, 然后执行它的命令. sim:build表示`sim`目标依赖于`build`目标, 会先执行build, 然后来执行sim.
#`@`的作用是 不在终端打印这条命令本身，但仍然会执行它.
sim:build	
	$(call git_commit, "sim RTL") # DO NOT REMOVE THIS LINE!!! 这行代码在每次运行`make sim`后自动执行一次, 在本地仓库执行一次提交.
	@echo "azazel's running simulation..."
	@./obj_dir/Vtop		#运行build好的可执行文件Vtop.
	
build:
	verilator --cc --exe --build -j 0 -Wall --trace sim_main.cpp top.v
	@echo "azazel's build completed!"




#把上一级那个Makefile直接插入过来了. 会利用其上的内容.
include ../Makefile
