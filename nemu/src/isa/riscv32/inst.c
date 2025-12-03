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
riscv32 ISA 的指令解码与执行.
***************************************************************************************/



/*********************************************************************************
C语言的位域(bit-field)语法:
int x : 5;
上述语句定义int类型变量x,
它在逻辑上占用5bit. 即, 编译器只会解释x的低5bit, 高位会被忽略. 但是物理存储上C语言还是会给x分配4字节(32bit)的空间...
**********************************************************************************/




/**********************************************************************************
GCC的语句表达式语法扩展:

一个语句表达式长这样:
({
    statement1;
    statement2;
    ...
    final_expression;
})

tips:    
1. 整个语句表达式 ( { ... } ) 的结果值 = 最后一条 表达式 的值.
2. （最后一行必须是 expression，而不是 statement）.

//简单的例子:
int y = 
({
    int a = 10;
    int b = 20;
    a + b;   // ← 这个就是返回值
});
上述代码等价于int y = 30;
**********************************************************************************/


#include "local-include/reg.h"
#include <cpu/cpu.h>
#include <cpu/ifetch.h>
#include <cpu/decode.h>

//便捷宏. gpr(general purpose register)通用寄存器.
#define R(i) gpr(i)
// 读取寄存器值函数.  Mr(addr, len) 读.
#define Mr vaddr_read
// 写入寄存器值函数. Mw(addr, len, val) 写.
#define Mw vaddr_write

// 指令类型枚举.
enum {
  TYPE_I, //I型. addi, jalr, lw 立即数型
  TYPE_U, //U型. lui, auipc 上位立即数型
  TYPE_S, //S型. sw 存储型
  TYPE_J, //J型. jal 跳转型
  TYPE_N, // none
};

// 读取源寄存器. src1R()
// 宏必须在所有上下文中都表现成一条“单语句”，而 do{ }while(0) 是唯一安全的写法.
#define src1R() do { *src1 = R(rs1); } while (0)
#define src2R() do { *src2 = R(rs2); } while (0)

// 立即数符号扩展宏.
// BITS宏: BITS(x, hi, lo): 从x中提取从lo到hi的位段. 如BITS(0b11110000, 7,4) = 0b1111
// SEXT(Sign EXTend, 符号扩展)宏: `SEXT(x, len);` 把len位长的数x[有符号扩展]为64bit数. len是x的原始位宽. 如SEXT(0b1111,4) = 0xffffffff, SEXT(0b0111,4) = 0x00000007
  // SEXT宏使用了 GCC的语句表达式语法扩展!!
  // 1. 定义一个匿名结构体, 里面有一个len位长的带符号位域n: struct { int64_t n : len; } 紧接着声明这个结构体变量`__x`, 把__x内存的那个位域初始化为x. 
  //{ .n = x }是C99指定初始化器写法别忘了. 就好像struct Point p = { .x = 10, .y = 20 };
  // 2. 把__x.n进行uint64_t强制类型转换, 返回. 这样就实现了符号扩展.
// I型立即数:
#define immI() do { *imm = SEXT(BITS(i, 31, 20), 12); } while(0)
#define immU() do { *imm = SEXT(BITS(i, 31, 12), 20) << 12; } while(0)
#define immS() do { *imm = (SEXT(BITS(i, 31, 25), 7) << 5) | BITS(i, 11, 7); } while(0)
#define immJ() do { *imm = SEXT((BITS(i, 31, 31) << 20) | (BITS(i, 19, 12) << 12) | (BITS(i, 20, 20) << 11) | (BITS(i, 30, 21) << 1), 21); } while(0)

static void decode_operand(Decode *s, int *rd, word_t *src1, word_t *src2, word_t *imm, int type) {
  uint32_t i = s->isa.inst.val;
  int rs1 = BITS(i, 19, 15);
  int rs2 = BITS(i, 24, 20);
  *rd     = BITS(i, 11, 7);
  switch (type) {
    case TYPE_I: src1R();          immI(); break;
    case TYPE_U:                   immU(); break;
    case TYPE_S: src1R(); src2R(); immS(); break;
    case TYPE_J:                   immJ(); break;
  }
}

//指令解码和执行函数. Decode *s是解码器状态结构体指针, 包含了当前指令的PC和指令本身.
//s->pc是当前指令的PC, s->snpc是静态下一条指令的PC(PC+4), s->dnpc是动态下一条指令的PC.
static int decode_exec(Decode *s) {
//
  int rd = 0;
  word_t src1 = 0, src2 = 0, imm = 0;
  s->dnpc = s->snpc;

//INSTPAT_INST(instruction pattern, 指令模式匹配)宏: 取出当前指令的原始 32 位机器码.
#define INSTPAT_INST(s) ((s)->isa.inst.val)

//INSTPAT_MATCH宏: 当指令匹配时：调用 decode_operand() 来解析寄存器号与立即数；执行宏体中传入的执行语句（__VA_ARGS__）。
#define INSTPAT_MATCH(s, name, type, ... /* execute body */ ) { \
  decode_operand(s, &rd, &src1, &src2, &imm, concat(TYPE_, type)); \
  __VA_ARGS__ ; \
}

  INSTPAT_START();
  // U型指令
  INSTPAT("??????? ????? ????? ??? ????? 00101 11", auipc  , U, R(rd) = s->pc + imm);
  // I型指令
  INSTPAT("??????? ????? ????? 000 ????? 00100 11", addi   , I, R(rd) = src1 + imm);
  INSTPAT("??????? ????? ????? 000 ????? 11001 11", jalr   , I, R(rd) = s->snpc; s->dnpc = (src1 + imm) & ~1);  // jalr rd, rs1, imm
  INSTPAT("??????? ????? ????? 100 ????? 00000 11", lbu    , I, R(rd) = Mr(src1 + imm, 1));
  // S型指令
  INSTPAT("??????? ????? ????? 000 ????? 01000 11", sb     , S, Mw(src1 + imm, 1, src2));
  INSTPAT("??????? ????? ????? 010 ????? 01000 11", sw     , S, Mw(src1 + imm, 4, src2));  // sw rs2, imm(rs1)
  // J型指令
  INSTPAT("??????? ????? ????? ??? ????? 11011 11", jal    , J, R(rd) = s->snpc; s->dnpc = s->pc + imm);  // jal rd, imm
  //
  INSTPAT("0000000 00001 00000 000 00000 11100 11", ebreak , N, NEMUTRAP(s->pc, R(10))); // R(10) is $a0
  // 匹配不到任何已知模式时, 调用INV()报错. invalid instruction
  INSTPAT("??????? ????? ????? ??? ????? ????? ??", inv    , N, INV(s->pc));
  INSTPAT_END();

  R(0) = 0; // reset $zero to 0

  return 0;
}

int isa_exec_once(Decode *s) {
  s->isa.inst.val = inst_fetch(&s->snpc, 4);
  return decode_exec(s);
}


















