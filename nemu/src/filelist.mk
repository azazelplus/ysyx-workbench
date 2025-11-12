#***************************************************************************************
# Copyright (c) 2014-2022 Zihao Yu, Nanjing University
#
# NEMU is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
#
# See the Mulan PSL v2 for more details.
# 
#**************************************************************************************/

# 将nemu-main.c加入到SRCS-y中. 这是显然的, nemu-main.c肯定要参与编译.
# SRCS-y: 参与编译的源文件的候选集合. 见nemu/Makefile
SRCS-y += src/nemu-main.c

# 将src/cpu, src/monitor, src/utils 加入到DIRS-y中.
# DIRS-y: 参与编译的目录. 见nemu/Makefile
DIRS-y += src/cpu src/monitor src/utils

# 通过定义CONFIG_MODE_SYSTEM = y, 可以使下面这条指令做这件事: 把memory加入DIRS-y(DIRS-y是参与编译的目录. 见nemu/Makefile)中.
DIRS-$(CONFIG_MODE_SYSTEM) += src/memory
# 同理, 通过定义CONFIG_MODE_USER = y, 可以使下面这条指令做这件事: 把device加入DIRS-y中.
DIRS-BLACKLIST-$(CONFIG_TARGET_AM) += src/monitor/sdb

SHARE = $(if $(CONFIG_TARGET_SHARE),1,0)
LIBS += $(if $(CONFIG_TARGET_NATIVE_ELF),-lreadline -ldl -pie,)

ifdef mainargs
ASFLAGS += -DBIN_PATH=\"$(mainargs)\"
endif
SRCS-$(CONFIG_TARGET_AM) += src/am-bin.S
.PHONY: src/am-bin.S
