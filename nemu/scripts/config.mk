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
#**************************************************************************************/

COLOR_RED := $(shell echo "\033[1;31m")
COLOR_END := $(shell echo "\033[0m")

ifeq ($(wildcard .config),)
$(warning $(COLOR_RED)Warning: .config does not exists!$(COLOR_END))
$(warning $(COLOR_RED)To build the project, first run 'make menuconfig'.$(COLOR_END))
endif

# `@`让命令静默执行, quiet
Q            := @
# Kconfig(程序)的路径
KCONFIG_PATH := $(NEMU_HOME)/tools/kconfig
# fixdep工具的路径
FIXDEP_PATH  := $(NEMU_HOME)/tools/fixdep
# Kconfig文件(配置描述文件)的路径.
Kconfig      := $(NEMU_HOME)/Kconfig
# 扩展变量rm-distclean, 清理更多文件.
rm-distclean += include/generated include/config .config .config.old
silent := -s


CONF   := $(KCONFIG_PATH)/build/conf

MCONF  := $(KCONFIG_PATH)/build/mconf

FIXDEP := $(FIXDEP_PATH)/build/fixdep


# 用来执行`conf --syncconfig Kconfig`, 生成配置.
$(CONF):
	$(Q)$(MAKE) $(silent) -C $(KCONFIG_PATH) NAME=conf
# 用来执行`mconf Kconfig`, 启动菜单配置界面.
$(MCONF):
	$(Q)$(MAKE) $(silent) -C $(KCONFIG_PATH) NAME=mconf
# 用来执行`fixdep`, 修复依赖关系.
$(FIXDEP):
	$(Q)$(MAKE) $(silent) -C $(FIXDEP_PATH)


# 该目标启动配置.
# $(MCONF) → 菜单界面程序
# $(CONF) → 配置同步程序
# $(FIXDEP) → 依赖关系修复工具
menuconfig: $(MCONF) $(CONF) $(FIXDEP)
# 执行命令`mconf nemu/Kconfig`, 启动菜单配置界面
	$(Q)$(MCONF) $(Kconfig)
# 同步配置
	$(Q)$(CONF) $(silent) --syncconfig $(Kconfig)

savedefconfig: $(CONF)
	$(Q)$< $(silent) --$@=configs/defconfig $(Kconfig)

%defconfig: $(CONF) $(FIXDEP)
	$(Q)$< $(silent) --defconfig=configs/$@ $(Kconfig)
	$(Q)$< $(silent) --syncconfig $(Kconfig)

.PHONY: menuconfig savedefconfig defconfig

# Help text used by make help
help:
	@echo  '  menuconfig	  - Update current config utilising a menu based program'
	@echo  '  savedefconfig   - Save current config as configs/defconfig (minimal config)'

distclean: clean
	-@rm -rf $(rm-distclean)

.PHONY: help distclean

define call_fixdep
	@$(FIXDEP) $(1) $(2) unused > $(1).tmp
	@mv $(1).tmp $(1)
endef
