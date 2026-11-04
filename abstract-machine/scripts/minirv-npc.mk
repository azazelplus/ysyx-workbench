# 架构mk, 会被$(AM_HOME)/Makefile在参数ARCH = minirv-npc时include.
# 用于 npc 多周期握手处理器

include $(AM_HOME)/scripts/isa/riscv.mk
include $(AM_HOME)/scripts/platform/npc.mk

export PATH := $(PATH):$(abspath $(AM_HOME)/tools/minirv)
CC = minirv-gcc
AS = minirv-gcc
CXX = minirv-g++

COMMON_CFLAGS += -march=rv32e_zicsr -mabi=ilp32e  # overwrite
LDFLAGS       += -melf32lriscv                    # overwrite

AM_SRCS += riscv/libgcc/div.S \
           riscv/libgcc/muldi3.S \
           riscv/libgcc/multi3.c \
           riscv/libgcc/ashldi3.c \
           riscv/libgcc/unused.c
