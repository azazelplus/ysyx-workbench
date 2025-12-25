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

//便捷宏. gpr(general purpose register)通用寄存器宏, 返回第i个寄存器的值.
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
  TYPE_R, //R型. add, sub, xor 寄存器型
  TYPE_B, //B型. beq, bne 分支型
  TYPE_N, // none
};


// 读取源寄存器宏. 
// `src1R();`即 `*src1 = gpr(rs1);`, 读取rs1(期望是一个int)号寄存器的值存到 *src1中.
// `src2R();`                        读取rs2(期望是一个int)号寄存器的值存到 *src2中.
// 宏必须在所有上下文中都表现成一条“单语句”，而 do{ }while(0) 是唯一安全的写法.
#define src1R() do { *src1 = R(rs1); } while (0)
#define src2R() do { *src2 = R(rs2); } while (0)

// imm系列宏: 提取指令的立即数部分.
// BITS宏: BITS(x, hi, lo): 从x中提取从lo到hi的位段. 如BITS(0b11110000, 7,4) = 0b1111
// SEXT(Sign EXTend, 符号扩展)宏: `SEXT(x, len);` 把len位长的数x[有符号扩展]为64bit数. len是x的原始位宽. 如SEXT(0b1111,4) = 0xffffffff, SEXT(0b0111,4) = 0x00000007
  // SEXT宏使用了 GCC的语句表达式语法扩展!!
  // 1. 定义一个匿名结构体, 里面有一个len位长的带符号位域n: struct { int64_t n : len; } 紧接着声明这个结构体变量`__x`, 把__x内存的那个位域初始化为x. 
  //{ .n = x }是C99指定初始化器写法别忘了. 就好像struct Point p = { .x = 10, .y = 20 };
  // 2. 把__x.n进行uint64_t强制类型转换, 返回. 这样就实现了符号扩展.
// 下面的宏不需要传入参数. 首先约定当前作用域下: i是32位指令机器码; imm是立即数指针. 

//I型指令的立即数为31:20位共12位. immI()取出I型指令i中的立即数, 扩展为64bit然后存入*imm.
#define immI() do { *imm = SEXT(BITS(i, 31, 20), 12); } while(0)
//U型指令的立即数为31:12位共20位并左移12位. immU()取出U型指令i中的立即数, 扩展为64bit然后存入*imm.
#define immU() do { *imm = SEXT(BITS(i, 31, 12), 20) << 12; } while(0)
//S型指令的立即数由31:25和11:7两段组成共12位. immS()取出S型指令i中的立即数, 扩展为64bit然后存入*imm.
// |31        25|24   20|19   15|14     12|11        7|6        0|
// |imm[11:5]   | rs2  |  rs1   | funct3  |  imm[4:0] |  opcode  |
#define immS() do { *imm = (SEXT(BITS(i, 31, 25), 7) << 5) | BITS(i, 11, 7); } while(0)
// J型指令的立即数 imm[20:1] 被拆分成四段藏在机器码不同位置。
// |31       |30        21|20      |19                12|11     7|6        0|
// |imm[20]  |imm[10:1]   |imm[11] | imm[19:12]         |   rd   |  opcode  |
#define immJ() do { *imm = SEXT((BITS(i, 31, 31) << 20) | (BITS(i, 19, 12) << 12) | (BITS(i, 20, 20) << 11) | (BITS(i, 30, 21) << 1), 21); } while(0)
// B型指令的立即数 imm[12:1] 被拆分成四段藏在机器码不同位置。
// |31       |30        25|24   20|19   15|14     12|11        8|7       |6        0|
// |imm[12]  |imm[10:5]   | rs2   |  rs1  | funct3  |  imm[4:1] |imm[11] |  opcode  |
#define immB() do { *imm = SEXT((BITS(i, 31, 31) << 12) | (BITS(i, 7, 7) << 11) | (BITS(i, 30, 25) << 5) | (BITS(i, 11, 8) << 1), 13); } while(0)


// decode_operand()函数: 提取指令的(rd, src1, src2, imm)每个部分并存到对应变量中.
// Decode *s是解码器状态结构体指针, 包含了当前指令的PC和指令本身.
// 接收指令类型type, 以及通过按指针传递来返回rd, src1, src2, imm.
static void decode_operand(Decode *s, int *rd, word_t *src1, word_t *src2, word_t *imm, int type) {
  uint32_t i = s->isa.inst.val;

  //对所有类型的指令都通用的部分: 提取rs1, rs2, rd寄存器号.
  int rs1 = BITS(i, 19, 15);
  int rs2 = BITS(i, 24, 20);
  *rd     = BITS(i, 11, 7);

  switch (type) {
    //I型指令: 需要rs1, 不需要rs2, 需要imm.
    case TYPE_I: src1R();          immI(); break;
    //U型指令: 不需要rs1, rs2, 只需要imm.
    case TYPE_U:                   immU(); break;
    //S型指令: 需要rs1, rs2, 需要imm.
    case TYPE_S: src1R(); src2R(); immS(); break;
    //J型指令: 不需要rs1, rs2, 只需要imm.
    case TYPE_J:                   immJ(); break;
    //R型指令: 需要rs1, rs2, 不需要imm.
    case TYPE_R: src1R(); src2R();         break;
    //B型指令: 需要rs1, rs2, 需要imm.
    case TYPE_B: src1R(); src2R(); immB(); break;
  }
}



// decode_exec()是指令解码与执行函数. 
// 接收要解码的Decode结构体s.
// tips: Decode结构体的内容:  s->pc是当前指令的PC, s->snpc是静态下一条指令的PC(PC+4), s->dnpc是动态下一条指令的PC.
static int decode_exec(Decode *s) {

//初始化局部状态
  int rd = 0;
  word_t src1 = 0, src2 = 0, imm = 0;
  s->dnpc = s->snpc;


//INSTPAT_INST取指令宏: 取出结构体存储的当前指令的 32 bit机器码.
//期望传入s是Decode结构体. 取出Decode结构体的成员isa.inst.val
#define INSTPAT_INST(s) ((s)->isa.inst.val)


//INSTPAT_MATCH指令模式匹配宏. 它的核心是接收一个type参数, 然后调用decode_operand()函数对当前作用域的(s, &rd, &src1, &src2, &imm, type)解码操作. 同时它会执行__VA_ARGS__的命令部分.

//它的调用层级是: INSTPAT -> INSTPAT_MATCH -> decode_operand()
//它调用了decode_operand(Decode *s, int *rd, word_t *src1, word_t *src2, word_t *imm, int type)函数(提取指令的(rd, src1, src2, imm)每个部分并存到对应变量中.). 所以使用该宏时要确保当前作用域下有变量rd, src1, src2, imm定义好.
// 期望传入参数: 当前正在解码的Decode结构体s; 指令名name; 指令类型type; 指令行为代码.
// 但是注意到传给INSTPAT_MATCH的 第二个参数 name 没有在宏内出现, 也就是没有用到, 被丢掉了.

// 我们以decode_exec()函数内的使用为例说明:
// INSTPAT("??????? ????? ????? ??? ????? 00101 11", auipc  , U, R(rd) = s->pc + imm);
// 其中, INSTPAT_MATCH会接收的参数是: INSTPAT_MATCH(s, ##__VA_ARGS__);
// 在本例中, 接受的s参数是当前作用域里的s变量; 而接受的##__VA_ARGS__是传入的`auipc  , U, R(rd) = s->pc + imm`这三段.
// 它们被解析为INSTPAT_MATCH的形参name, type, ... 
// 最终, 在INSTPAT_MATCH宏体内:
// 参数"name"接收到"auipc"并且没有用;
// 参数"type"接收到"U", 用来拼成"type_U"传给decode_operand函数工作.
// 参数...接收到"R(rd) = s->pc + imm", ...就是宏体内的__VA_ARGS__. 它作为执行代码被执行. 这也是auipc指令实际做的事: 把立即数（左移 12）与当前指令地址相加.
#define INSTPAT_MATCH(s, name, type, ... /* execute body */ ) { \
  decode_operand(s, &rd, &src1, &src2, &imm, concat(TYPE_, type)); \
  __VA_ARGS__ ; \
}



//模式匹配块开始
  INSTPAT_START();
  
  // 匹配U型指令. 展开INSTPAT->展开INSTPAT_MATCH->调用decode_operand. 最终展开的代码:
/*
do {
  // 1. 解析模式字符串，生成掩码
  //    pattern "??????? ????? ????? ??? ????? 00101 11" 
  //    → key=0b0010111, mask=0b1111111, shift=0
  //    (只检查 opcode 部分 [6:0] = 0010111)
  uint64_t key, mask, shift;
  pattern_decode("??????? ????? ????? ??? ????? 00101 11", 37, &key, &mask, &shift);
  
  // 2. 用掩码检查当前指令是否匹配 auipc 的 opcode
  if ((((uint64_t)(s->isa.inst.val) >> shift) & mask) == key) {
    
    // 3. 匹配成功！解码操作数（U型指令只需要 rd 和 imm）
    decode_operand(s, &rd, &src1, &src2, &imm, TYPE_U);
    
    // 4. 执行 auipc 指令：rd = PC + imm
    gpr(rd) = s->pc + imm;
    
    // 5. 跳转到模式匹配块末尾，结束解码
    goto *(__instpat_end);
  }
} while (0)
*/
  INSTPAT("??????? ????? ????? ??? ????? 00101 11", auipc  , U, R(rd) = s->pc + imm);
  INSTPAT("??????? ????? ????? ??? ????? 01101 11", lui    , U, R(rd) = imm);  // lui rd, imm
  
  // I型指令
  INSTPAT("??????? ????? ????? 000 ????? 00100 11", addi   , I, R(rd) = src1 + imm);
  INSTPAT("??????? ????? ????? 011 ????? 00100 11", sltiu  , I, R(rd) = (src1 < (word_t)imm) ? 1 : 0);  // sltiu rd, rs1, imm (用于seqz)
  INSTPAT("??????? ????? ????? 111 ????? 00100 11", andi   , I, R(rd) = src1 & imm);  // andi rd, rs1, imm
  INSTPAT("0000000 ????? ????? 001 ????? 00100 11", slli   , I, R(rd) = src1 << BITS(imm, 4, 0));  // slli rd, rs1, shamt (逻辑左移)
  INSTPAT("??????? ????? ????? 000 ????? 11001 11", jalr   , I, R(rd) = s->snpc; s->dnpc = (src1 + imm) & ~1);  // jalr rd, rs1, imm
  INSTPAT("??????? ????? ????? 010 ????? 00000 11", lw     , I, R(rd) = Mr(src1 + imm, 4));  // lw rd, imm(rs1)
  INSTPAT("??????? ????? ????? 100 ????? 00000 11", lbu    , I, R(rd) = Mr(src1 + imm, 1));
  
  // S型指令
  // |31        25|24   20|19   15|14     12|11        7|6        0|
  // |imm[11:5]   | rs2  |  rs1   | funct3  |  imm[4:0] |  opcode  |
  INSTPAT("??????? ????? ????? 000 ????? 01000 11", sb     , S, Mw(src1 + imm, 1, src2));
  INSTPAT("??????? ????? ????? 010 ????? 01000 11", sw     , S, Mw(src1 + imm, 4, src2));  // sw rs2, imm(rs1)
  
  // R型指令
  // |31       25|24   20|19   15|14     12|11     7|6        0|
  // | funct7    | rs2  |  rs1   | funct3  |   rd   |  opcode  |
  INSTPAT("0000000 ????? ????? 000 ????? 01100 11", add    , R, R(rd) = src1 + src2);  // add rd, rs1, rs2
  INSTPAT("0100000 ????? ????? 000 ????? 01100 11", sub    , R, R(rd) = src1 - src2);  // sub rd, rs1, rs2
  INSTPAT("0000000 ????? ????? 100 ????? 01100 11", xor    , R, R(rd) = src1 ^ src2);  // xor rd, rs1, rs2
  INSTPAT("0000000 ????? ????? 110 ????? 01100 11", or     , R, R(rd) = src1 | src2);  // or rd, rs1, rs2
  INSTPAT("0000000 ????? ????? 010 ????? 01100 11", slt    , R, R(rd) = ((sword_t)src1 < (sword_t)src2) ? 1 : 0);  // slt rd, rs1, rs2 (有符号比较)
  INSTPAT("0000000 ????? ????? 011 ????? 01100 11", sltu   , R, R(rd) = (src1 < src2) ? 1 : 0);  // sltu rd, rs1, rs2
  // RV32M 扩展 - 乘除法指令
  INSTPAT("0000001 ????? ????? 000 ????? 01100 11", mul    , R, R(rd) = src1 * src2);  // mul rd, rs1, rs2 (取低32位)
  INSTPAT("0000001 ????? ????? 001 ????? 01100 11", mulh   , R, R(rd) = (int64_t)(sword_t)src1 * (int64_t)(sword_t)src2 >> 32);  // mulh rd, rs1, rs2 (有符号乘法取高32位)
  INSTPAT("0000001 ????? ????? 100 ????? 01100 11", div    , R, R(rd) = (sword_t)src1 / (sword_t)src2);  // div rd, rs1, rs2 (有符号除法)
  
  // B型指令
  // |31       |30        25|24   20|19   15|14     12|11        8|7       |6        0|
  // |imm[12]  |imm[10:5]   | rs2   |  rs1  | funct3  |  imm[4:1] |imm[11] |  opcode  |
  INSTPAT("??????? ????? ????? 000 ????? 11000 11", beq    , B, s->dnpc = (src1 == src2) ? s->pc + imm : s->snpc);  // beq rs1, rs2, imm
  INSTPAT("??????? ????? ????? 001 ????? 11000 11", bne    , B, s->dnpc = (src1 != src2) ? s->pc + imm : s->snpc);  // bne rs1, rs2, imm
  INSTPAT("??????? ????? ????? 100 ????? 11000 11", blt    , B, s->dnpc = ((sword_t)src1 < (sword_t)src2) ? s->pc + imm : s->snpc);  // blt rs1, rs2, imm (有符号比较)
  INSTPAT("??????? ????? ????? 101 ????? 11000 11", bge    , B, s->dnpc = ((sword_t)src1 >= (sword_t)src2) ? s->pc + imm : s->snpc);  // bge rs1, rs2, imm (有符号比较)
  INSTPAT("??????? ????? ????? 110 ????? 11000 11", bltu   , B, s->dnpc = ((word_t)src1 <  (word_t)src2) ? s->pc + imm : s->snpc); // bltu rs1, rs2, imm (unsigned)
  INSTPAT("??????? ????? ????? 111 ????? 11000 11", bgeu   , B, s->dnpc = ((word_t)src1 >= (word_t)src2) ? s->pc + imm : s->snpc); // bgeu rs1, rs2, imm (unsigned)
  
  // J型指令
  // |31       |30        21|20      |19                12|11     7|6        0|
  // |imm[20]  |imm[10:1]   |imm[11] | imm[19:12]         |   rd   |  opcode  |
  INSTPAT("??????? ????? ????? ??? ????? 11011 11", jal    , J, R(rd) = s->snpc; s->dnpc = s->pc + imm);  // `R(rd) = s->snpc;`用来link; `s->dnpc = s->pc + imm`用来跳转.

  // N型指令
  // |31        25|24   20|19   15|14     12|11        7|6        0|
  // |0000000     | rs2  |  rs1   | funct3  |  rd       |  opcode  |
  INSTPAT("0000000 00001 00000 000 00000 11100 11", ebreak , N, NEMUTRAP(s->pc, R(10))); // R(10) is $a0
  // 匹配不到任何已知模式时, 调用INV()报错. invalid instruction
  INSTPAT("??????? ????? ????? ??? ????? ????? ??", inv    , N, INV(s->pc));
  INSTPAT_END();
  //模式匹配块结束



  R(0) = 0; // reset $zero to 0

  return 0;
}




// 查找一元运算符后面的最小操作数范围.
int isa_exec_once(Decode *s) {
  s->isa.inst.val = inst_fetch(&s->snpc, 4);
  return decode_exec(s);
}


















