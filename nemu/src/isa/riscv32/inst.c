/***************************************************************************************
riscv32 ISA 的指令解码与执行. IDU+EXU
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
#include <isa.h>  // for isa_raise_intr()

//便捷宏. gpr(general purpose register)通用寄存器宏, 返回第i个寄存器的值.
#define R(i) gpr(i)
// 读取寄存器值函数.  Mr(addr, len) 读.
#define Mr vaddr_read
// 写入寄存器值函数. Mw(addr, len, val) 写.
#define Mw vaddr_write

// ==================== CSR 读写辅助函数 ====================

// CSR 地址常量
#define CSR_MSTATUS   0x300
#define CSR_MTVEC     0x305
#define CSR_MEPC      0x341
#define CSR_MCAUSE    0x342
#define CSR_MVENDORID 0xF11
#define CSR_MARCHID   0xF12

// csr_read: 根据 12 位 CSR 地址读取 CSR 值
static word_t csr_read(uint32_t addr) {
  switch (addr) {
    case CSR_MSTATUS:   return cpu.mstatus;
    case CSR_MTVEC:     return cpu.mtvec;
    case CSR_MEPC:      return cpu.mepc;
    case CSR_MCAUSE:    return cpu.mcause;
    case CSR_MVENDORID: return 0x79737978;  // ysyx
    case CSR_MARCHID:   return 0x17f270e;
    default:            return 0;
  }
}

// csr_write: 根据 12 位 CSR 地址写入 CSR 值 (只读寄存器写入无效)
static void csr_write(uint32_t addr, word_t val) {
  switch (addr) {
    case CSR_MSTATUS:   cpu.mstatus = val; break;
    case CSR_MTVEC:     cpu.mtvec   = val; break;
    case CSR_MEPC:      cpu.mepc    = val; break;
    case CSR_MCAUSE:    cpu.mcause  = val; break;
    // mvendorid, marchid 为只读, 写入忽略
    default: break;
  }
}

// 指令类型枚举. 给decode_operand()看的, 让它知道该怎麽decode当前inst
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
  // SEXT宏使用了 GCC的语句表达式语法扩展...
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

/** 
decode_operand()函数: 提取指令的(rd, src1, src2, imm)每个部分并存到对应变量中.
* @param s : 解码器状态结构体指针, 包含了当前指令的PC和指令本身.
* @param rd : 指令的目的寄存器编号结果写入位置.
* @param src1: 指令的第一个源寄存器值写入位置.
* @param src2: 指令的第二个源寄存器值写入位置.
* @param imm: 指令的立即数值写入位置.
* @param type: 指令类型, 决定了指令格式和立即数
*/
static void decode_operand(Decode *s, int *rd, word_t *src1, word_t *src2, word_t *imm, int type) {
  uint32_t i = s->isa.inst.val; //i是当前指令.

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



// decode_exec()是指令 解码+执行 
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
  
/***
匹配指令. 
* @param 1 第一个参数是指令模式字符串; 
* @param 2 第二个参数是指令名(仅仅为了易读); 
* @param 3 第三个参数是指令类型, 用来告诉decode_operand函数怎么解码; 
* @param 4...第四个往后的参数是指令行为. 也就是运行该指令要执行的所有语句.  R()访问GPR, 
*/
// 展开INSTPAT->展开INSTPAT_MATCH->调用decode_operand. 最终展开的代码(以U型为例)
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
  // U型指令
  INSTPAT("??????? ????? ????? ??? ????? 00101 11", auipc  , U, R(rd) = s->pc + imm);
  INSTPAT("??????? ????? ????? ??? ????? 01101 11", lui    , U, R(rd) = imm);  // lui rd, imm

  // 无条件跳转: jal & jalr
  INSTPAT("??????? ????? ????? ??? ????? 11011 11", jal    , J, R(rd) = s->snpc; s->dnpc = s->pc + imm);  // `R(rd) = s->snpc;`用来link; `s->dnpc = s->pc + imm`用来跳转.
  INSTPAT("??????? ????? ????? 000 ????? 11001 11", jalr   , I, R(rd) = s->snpc; s->dnpc = (src1 + imm) & ~1);

  // 分支跳转(B型指令)
  INSTPAT("??????? ????? ????? 000 ????? 11000 11", beq    , B, s->dnpc = (src1 == src2) ? s->pc + imm : s->snpc);
  INSTPAT("??????? ????? ????? 001 ????? 11000 11", bne    , B, s->dnpc = (src1 != src2) ? s->pc + imm : s->snpc);
  INSTPAT("??????? ????? ????? 100 ????? 11000 11", blt    , B, s->dnpc = ((sword_t)src1 < (sword_t)src2) ? s->pc + imm : s->snpc);
  INSTPAT("??????? ????? ????? 101 ????? 11000 11", bge    , B, s->dnpc = ((sword_t)src1 >= (sword_t)src2) ? s->pc + imm : s->snpc);
  INSTPAT("??????? ????? ????? 110 ????? 11000 11", bltu   , B, s->dnpc = (src1 < src2) ? s->pc + imm : s->snpc);
  INSTPAT("??????? ????? ????? 111 ????? 11000 11", bgeu   , B, s->dnpc = (src1 >= src2) ? s->pc + imm : s->snpc);

  // load & store
  INSTPAT("??????? ????? ????? 000 ????? 00000 11", lb     , I, R(rd) = SEXT(Mr(src1 + imm, 1), 8));
  INSTPAT("??????? ????? ????? 001 ????? 00000 11", lh     , I, R(rd) = SEXT(Mr(src1 + imm, 2), 16));
  INSTPAT("??????? ????? ????? 010 ????? 00000 11", lw     , I, R(rd) = Mr(src1 + imm, 4));
  INSTPAT("??????? ????? ????? 100 ????? 00000 11", lbu    , I, R(rd) = Mr(src1 + imm, 1));
  INSTPAT("??????? ????? ????? 101 ????? 00000 11", lhu    , I, R(rd) = Mr(src1 + imm, 2));

  INSTPAT("??????? ????? ????? 000 ????? 01000 11", sb     , S, Mw(src1 + imm, 1, src2));
  INSTPAT("??????? ????? ????? 001 ????? 01000 11", sh     , S, Mw(src1 + imm, 2, src2));
  INSTPAT("??????? ????? ????? 010 ????? 01000 11", sw     , S, Mw(src1 + imm, 4, src2));

  // op_imm指令(opcode=0010011. 它们都是I型指令, 行为是rs1和imm计算后存入rd)
  INSTPAT("??????? ????? ????? 000 ????? 00100 11", addi   , I, R(rd) = src1 + imm);
  INSTPAT("??????? ????? ????? 010 ????? 00100 11", slti   , I, R(rd) = ((sword_t)src1 < (sword_t)imm) ? 1 : 0);
  INSTPAT("??????? ????? ????? 011 ????? 00100 11", sltiu  , I, R(rd) = (src1 < (word_t)imm) ? 1 : 0);
  INSTPAT("??????? ????? ????? 100 ????? 00100 11", xori   , I, R(rd) = src1 ^ imm);
  INSTPAT("??????? ????? ????? 110 ????? 00100 11", ori    , I, R(rd) = src1 | imm);
  INSTPAT("??????? ????? ????? 111 ????? 00100 11", andi   , I, R(rd) = src1 & imm);
  INSTPAT("0000000 ????? ????? 001 ????? 00100 11", slli   , I, R(rd) = src1 << (imm & 0x1f));
  INSTPAT("0000000 ????? ????? 101 ????? 00100 11", srli   , I, R(rd) = src1 >> (imm & 0x1f));
  INSTPAT("0100000 ????? ????? 101 ????? 00100 11", srai   , I, R(rd) = (sword_t)src1 >> (imm & 0x1f));
  

  // R型指令
  INSTPAT("0000000 ????? ????? 000 ????? 01100 11", add    , R, R(rd) = src1 + src2);
  INSTPAT("0100000 ????? ????? 000 ????? 01100 11", sub    , R, R(rd) = src1 - src2);
  INSTPAT("0000000 ????? ????? 001 ????? 01100 11", sll    , R, R(rd) = src1 << (src2 & 0x1f));
  INSTPAT("0000000 ????? ????? 010 ????? 01100 11", slt    , R, R(rd) = ((sword_t)src1 < (sword_t)src2) ? 1 : 0);
  INSTPAT("0000000 ????? ????? 011 ????? 01100 11", sltu   , R, R(rd) = (src1 < src2) ? 1 : 0);
  INSTPAT("0000000 ????? ????? 100 ????? 01100 11", xor    , R, R(rd) = src1 ^ src2);
  INSTPAT("0000000 ????? ????? 101 ????? 01100 11", srl    , R, R(rd) = src1 >> (src2 & 0x1f));
  INSTPAT("0100000 ????? ????? 101 ????? 01100 11", sra    , R, R(rd) = (sword_t)src1 >> (src2 & 0x1f));
  INSTPAT("0000000 ????? ????? 110 ????? 01100 11", or     , R, R(rd) = src1 | src2);
  INSTPAT("0000000 ????? ????? 111 ????? 01100 11", and    , R, R(rd) = src1 & src2);
  

  // fence指令
  //INSTPAT("??????? ????? ????? 000 ????? 00011 11", fence    , N, );
  //INSTPAT("??????? ????? ????? 001 ????? 00011 11", fence_i  , N, );


  // RV32M 扩展 - 乘除法指令
  INSTPAT("0000001 ????? ????? 000 ????? 01100 11", mul    , R, R(rd) = src1 * src2);
  INSTPAT("0000001 ????? ????? 001 ????? 01100 11", mulh   , R, R(rd) = ((int64_t)(sword_t)src1 * (int64_t)(sword_t)src2) >> 32);
  INSTPAT("0000001 ????? ????? 010 ????? 01100 11", mulhsu , R, R(rd) = ((int64_t)(sword_t)src1 * (uint64_t)src2) >> 32);
  INSTPAT("0000001 ????? ????? 011 ????? 01100 11", mulhu  , R, R(rd) = ((uint64_t)src1 * (uint64_t)src2) >> 32);

  INSTPAT("0000001 ????? ????? 100 ????? 01100 11", div    , R, R(rd) = 
  src2==0 ? -1 : ((sword_t)src1 / (sword_t)src2)
  );

  INSTPAT("0000001 ????? ????? 101 ????? 01100 11", divu   , R, R(rd) = 
  src2==0 ? -1 : (src1 / src2)
  );
  INSTPAT("0000001 ????? ????? 110 ????? 01100 11", rem    , R, R(rd) = 
  src2==0 ? src1 : ((sword_t)src1 % (sword_t)src2)
  );

  INSTPAT("0000001 ????? ????? 111 ????? 01100 11", remu   , R, R(rd) = 
  src2==0 ? src1 : (src1 % src2)
  );
  

  // ==================== System 指令 ====================
  // ecall: 调用函数isa_raise_intr()函数, 触发 M-mode 环境调用异常 (mcause=11), 跳转到 mtvec
  INSTPAT("0000000 00000 00000 000 00000 11100 11", ecall  , N, s->dnpc = isa_raise_intr(11, s->pc));
  // ebreak: 当前仍由 NEMU 仿真器截获作为 TRAP 处理
  INSTPAT("0000000 00001 00000 000 00000 11100 11", ebreak , N, NEMUTRAP(s->pc, R(10))); // R(10) is $a0
  // mret: 从异常返回, PC = mepc, 恢复 mstatus
  INSTPAT("0011000 00010 00000 000 00000 11100 11", mret   , N, s->dnpc = cpu.mepc; word_t mpie = (cpu.mstatus >> 7) & 1; cpu.mstatus = (cpu.mstatus & ~(1 << 3)) | (mpie << 3); cpu.mstatus |= (1 << 7));

  // CSR 指令 (opcode=1110011, funct3 区分 6 种)
  // 例如csrrw, 匹配成功后要执行的代码: 
  /** 
  word_t csr_addr = BITS(s->isa.inst.val, 31, 20);  // 从指令中提取出 CSR 地址 (12 位)
  word_t t = csr_read(csr_addr);                    // 读CSR寄存器, 保存旧值
  csr_write(csr_addr, src1);                  // 写CSR寄存器, 新值来自rs1
  R(rd) = t;                              // 将旧值写回目的寄存器rd
  */

  // csrrw  rd, csr, rs1 :  t=CSR[csr]; CSR[csr]=rs1;  rd=t
  INSTPAT("??????? ????? ????? 001 ????? 11100 11", csrrw  , I, word_t csr_addr = BITS(s->isa.inst.val, 31, 20); word_t t = csr_read(csr_addr); csr_write(csr_addr, src1); R(rd) = t);
  // csrrs  rd, csr, rs1 :  t=CSR[csr]; CSR[csr]=t|rs1; rd=t
  INSTPAT("??????? ????? ????? 010 ????? 11100 11", csrrs  , I, word_t csr_addr = BITS(s->isa.inst.val, 31, 20); word_t t = csr_read(csr_addr); csr_write(csr_addr, t | src1); R(rd) = t);
  // csrrc  rd, csr, rs1 :  t=CSR[csr]; CSR[csr]=t&~rs1; rd=t
  INSTPAT("??????? ????? ????? 011 ????? 11100 11", csrrc  , I, word_t csr_addr = BITS(s->isa.inst.val, 31, 20); word_t t = csr_read(csr_addr); csr_write(csr_addr, t & ~src1); R(rd) = t);
  // csrrwi rd, csr, zimm:  t=CSR[csr]; CSR[csr]=zimm; rd=t   (zimm = inst[19:15], 零扩展)
  INSTPAT("??????? ????? ????? 101 ????? 11100 11", csrrwi , I, word_t csr_addr = BITS(s->isa.inst.val, 31, 20); word_t zimm = BITS(s->isa.inst.val, 19, 15); word_t t = csr_read(csr_addr); csr_write(csr_addr, zimm); R(rd) = t);
  // csrrsi rd, csr, zimm:  t=CSR[csr]; CSR[csr]=t|zimm; rd=t
  INSTPAT("??????? ????? ????? 110 ????? 11100 11", csrrsi , I, word_t csr_addr = BITS(s->isa.inst.val, 31, 20); word_t zimm = BITS(s->isa.inst.val, 19, 15); word_t t = csr_read(csr_addr); csr_write(csr_addr, t | zimm); R(rd) = t);
  // csrrci rd, csr, zimm:  t=CSR[csr]; CSR[csr]=t&~zimm; rd=t
  INSTPAT("??????? ????? ????? 111 ????? 11100 11", csrrci , I, word_t csr_addr = BITS(s->isa.inst.val, 31, 20); word_t zimm = BITS(s->isa.inst.val, 19, 15); word_t t = csr_read(csr_addr); csr_write(csr_addr, t & ~zimm); R(rd) = t);

  // 匹配不到任何已知模式时, 调用INV()报错. invalid instruction
  INSTPAT("??????? ????? ????? ??? ????? ????? ??", inv    , N, INV(s->pc));



  INSTPAT_END();
  //模式匹配块结束


  // reset $zero to 0
  R(0) = 0; 

  return 0;
}




// (指定ISA的)对上下文结构体s进行一次单步执行: 取指 -> 译码执行
int isa_exec_once(Decode *s) {

  // 1. 取指(Instruction Fetch): 从 snpc 指示的内存位置读取 4 字节指令
  s->isa.inst.val = inst_fetch(&s->snpc, 4);

  // 2. 译码并执行(Decode & Execute): 解析指令并执行相应行为 (在 decode_exec 中通过模式匹配实现)
  return decode_exec(s);
}


















