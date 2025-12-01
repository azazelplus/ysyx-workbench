/***************************************************************************************
* Copyright (c) 2014-2022 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
//src/isa/riscv32/reg.c: RV32寄存器相关操作的实现.
***************************************************************************************/

#include <isa.h>
#include "local-include/reg.h"

const char *regs[] = {
  "$0", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
  "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
  "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
  "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

//打印RV32的32个通用寄存器(general purpose registers) + pc寄存器状态.
void isa_reg_display() {
  for (int i = 0; i < 32; i++) {
    printf("%s\t0x%08x\n", regs[i], cpu.gpr[i]);
  }
  printf("pc\t0x%08x\n", cpu.pc);
}


//根据寄存器名称字符串 s, 返回对应寄存器的值.
word_t isa_reg_str2val(const char *s, bool *success) {
  // 检查是否是 pc 寄存器
  if (strcmp(s, "pc") == 0) {
    *success = true;
    return cpu.pc;
  }
  
  // 遍历通用寄存器数组
  for (int i = 0; i < 32; i++) {
    if (strcmp(s, regs[i]) == 0) {
      *success = true;
      return cpu.gpr[i];
    }
  }
  
  // 也支持 x0-x31 格式
  if (s[0] == 'x' && s[1] >= '0' && s[1] <= '9') {
    int reg_no = atoi(s + 1);
    if (reg_no >= 0 && reg_no < 32) {
      *success = true;
      return cpu.gpr[reg_no];
    }
  }
  
  // 没找到匹配的寄存器
  *success = false;
  return 0;
}
