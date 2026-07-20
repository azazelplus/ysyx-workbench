#***************************************************************************************

# 这个config.mk用来
#**************************************************************************************

COLOR_RED := $(shell echo "\033[1;31m")
COLOR_END := $(shell echo "\033[0m")

ifeq ($(wildcard .config),)
$(warning $(COLOR_RED)Warning: .config does not exists!$(COLOR_END))
$(warning $(COLOR_RED)To build the project, first run 'make menuconfig'.$(COLOR_END))
endif

# `@`让命令静默执行, quiet
Q            := @
# kconfig目录的路径. 里面是Kconfig项目源码.
KCONFIG_PATH := $(NEMU_HOME)/tools/kconfig
# fixdep工具的路径.
FIXDEP_PATH  := $(NEMU_HOME)/tools/fixdep
# Kconfig文件(配置描述文件)的路径.
Kconfig      := $(NEMU_HOME)/Kconfig
# 扩展变量rm-distclean, 清理更多文件.
rm-distclean += include/generated include/config .config .config.old
silent := -s

# 即`nemu/tools/kconfig/build/conf`, 用来同步配置的可执行文件.
CONF   := $(KCONFIG_PATH)/build/conf

MCONF  := $(KCONFIG_PATH)/build/mconf
# fixdep工具生成的可执行文件路径.
FIXDEP := $(FIXDEP_PATH)/build/fixdep


# 执行命令`conf --syncconfig Kconfig`, 生成配置.
$(CONF):
	$(Q)$(MAKE) $(silent) -C $(KCONFIG_PATH) NAME=conf
# 用来执行`mconf Kconfig`, 启动菜单配置界面.
$(MCONF):
	$(Q)$(MAKE) $(silent) -C $(KCONFIG_PATH) NAME=mconf
# 用来执行可执行文件`nemu/tools/fixdep/build/fixdep`, 修复依赖关系.
$(FIXDEP):
	$(Q)$(MAKE) $(silent) -C $(FIXDEP_PATH)


# 伪目标menuconfig. 它是menuconfig应用 通过 Makefile 规则注入的入口点.
# $(Q) 让命令静默执行.
# $(MCONF) → menuconfig菜单界面程序
# $(CONF) → 配置同步程序
# $(FIXDEP) → 依赖关系修复工具
menuconfig: $(MCONF) $(CONF) $(FIXDEP)
# 执行命令`$(KCONFIG_PATH)/build/mconf nemu/Kconfig`, 启动菜单配置界面. 用户配置后, 将结果写入`nemu/.config`. 其中mconf是可执行文件, Kconfig是配置描述文件, 作为mconf的参数. 
	$(Q)$(MCONF) $(Kconfig)
# 同步配置. 执行`$(KCONFIG_PATH)/build/conf --syncconfig nemu/Kconfig`. 读取`config`用户配置, 生成一大堆文件.
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
