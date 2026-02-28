#***************************************************************************************
# See the Mulan PSL v2 for more details.
#
# filelist.mk是分布式收集mk.  它们都会被包含到nemu主Makefile中. 在nemu/Makefile中有两行:
#  
#  FILELIST_MK = $(shell find -L ./src -name "filelist.mk")	# 这一行命令会查找 src/ 下所有的 filelist.mk 文件
#  include $(FILELIST_MK)	# 并将它们全部包含进来
#**************************************************************************************/

ifneq ($(CONFIG_ITRACE)$(CONFIG_IQUEUE),)
CXXSRC = src/utils/disasm.cc
CXXFLAGS += $(shell llvm-config --cxxflags) -fPIE
LIBS += $(shell llvm-config --libs)
endif

ifdef CONFIG_FTRACE
SRCS += src/utils/ftrace.c
endif
