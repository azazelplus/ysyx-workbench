include $(AM_HOME)/scripts/isa/riscv.mk
include $(AM_HOME)/scripts/platform/npc.mk

COMMON_CFLAGS += -march=rv32e_zicsr -mabi=ilp32e  # overwrite
LDFLAGS       += -melf32lriscv                    # overwrite

AM_SRCS += riscv/libgcc/div.S \
           riscv/libgcc/muldi3.S \
           riscv/libgcc/multi3.c \
           riscv/libgcc/ashldi3.c \
           riscv/libgcc/unused.c
