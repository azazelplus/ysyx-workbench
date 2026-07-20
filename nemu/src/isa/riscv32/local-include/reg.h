/***************************************************************************************
* 寄存器相关.
***************************************************************************************/

#ifndef __RISCV_REG_H__
#define __RISCV_REG_H__

#include <common.h>

// 对寄存器索引进行检查, 确保在合法范围内.
static inline int check_reg_idx(int idx) {
  IFDEF(CONFIG_RT_CHECK, assert(idx >= 0 && idx < MUXDEF(CONFIG_RVE, 16, 32)));
  return idx;
}

// 安全地返回寄存器值
#define gpr(idx) (cpu.gpr[check_reg_idx(idx)])

// 返回寄存器名字.
static inline const char* reg_name(int idx) {
  extern const char* regs[];
  return regs[check_reg_idx(idx)];
}

#endif
