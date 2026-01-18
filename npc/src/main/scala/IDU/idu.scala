// MiniRV 译码单元 (Instruction Decode Unit)
package minirv.idu

import chisel3._
import chisel3.util._
import minirv._

/**
  * IDU - 译码单元
  * 
  * 功能：
  * 1. 解析指令字段 (opcode, rd, rs1, rs2, funct3, funct7)
  * 2. 生成立即数 (I/S/B/U/J 类型)
  * 3. 读取寄存器堆
  * 4. 生成控制信号
  */
class IDU extends Module {
  val io = IO(new Bundle {
    // ========== IFU->IDU ==========
    val in = Input(new IF2ID)  // 输入: IFU->IDU, 取到的(pc, inst)

    // ========== 读端口  IDU<->RegFile  ==========
    val rs1_addr = Output(UInt(Config.REG_ADDR_W.W))  // 读地址1. IDU->RegFile
    val rs2_addr = Output(UInt(Config.REG_ADDR_W.W))  // 读地址2. IDU->RegFile
    val rs1_data = Input(UInt(Config.XLEN.W))         // 读数据1. RegFile->IDU（顶层已做前递/旁路）
    val rs2_data = Input(UInt(Config.XLEN.W))         // 读数据2. RegFile->IDU（顶层已做前递/旁路）

    // ========== IDU->EXU ==========
    val out = Output(new ID2EX)  // 输出: IDU->EXU, 控制信号 + 操作数 + 立即数 + pc
  })

  val inst = io.in.inst

  // 指令字段切片解析
  val opcode = inst(6, 0)
  val rd     = inst(11, 7)
  val funct3 = inst(14, 12)
  val rs1    = inst(19, 15)
  val rs2    = inst(24, 20)
  val funct7 = inst(31, 25)

  // 六种立即数生成. Fill(n, x)将信号x复制n次.  I-type中inst(31)是imm的最高符号位, 需要符号位扩展.
  // B和J的立即数最后一位是隐含的0, 因为地址是%2对齐的.
  val imm_i = Cat(Fill(20, inst(31)), inst(31, 20))                              // I-type
  val imm_s = Cat(Fill(20, inst(31)), inst(31, 25), inst(11, 7))                 // S-type
  val imm_b = Cat(Fill(19, inst(31)), inst(31), inst(7), inst(30, 25), inst(11, 8), 0.U(1.W)) // B-type
  val imm_u = Cat(inst(31, 12), 0.U(12.W))                                        // U-type
  val imm_j = Cat(Fill(11, inst(31)), inst(31), inst(19, 12), inst(20), inst(30, 21), 0.U(1.W)) // J-type


  // 根据 opcode 选择六种立即数. 
  // MuxLookup生成查找表多路选择器. 它是组合逻辑块.
  // 完整签名: MuxLookup(key: UInt, default: T)(mapping: Seq[(UInt, T)]): T
  // MuxLookup这个allpy函数调用时跟了两个括号, 这是[currying]柯里化特性. 就是把函数的多个参数用多个有序括号给出. 这样好处是提高可读性 和 帮助编译器类型推断.
  // 其中, default是默认[值]. 键值对 匹配失败则返回这个默认[值].
  // 其中Seq中的元素类型是(UInt, T), 一个一个元组. 元组的第一个元素key必须是UInt类型, 比如`Opcode.OP_IMM`. 第二个元素是值, 类型T(任意).
  // -> 符号是创建元组的语法糖, A -> B 等同于 (A, B), 仅仅易读这是个键值对.
  val imm = MuxLookup(opcode, 0.U)(Seq(
    Opcode.OP_IMM -> imm_i,
    Opcode.LOAD   -> imm_i,
    Opcode.JALR   -> imm_i,
    Opcode.STORE  -> imm_s,
    Opcode.BRANCH -> imm_b,
    Opcode.LUI    -> imm_u,
    Opcode.AUIPC  -> imm_u,
    Opcode.JAL    -> imm_j
  ))



  // ALU 操作选择 (根据 opcode, funct3, funct7)
  // alu_op是控制信号, 接入ALU控制输入端, 决定ALU做什么运算. 默认为`ALUOp.ADD`, 即加法.
  // WireDefault相比Wire, 就是提供了默认驱动源.
  val alu_op = WireDefault(ALUOp.ADD)
  
  // R 和 I 指令的 ALU 操作由 funct3/funct7 决定.
  // chisel的等价比较为`===`, 逻辑或为`||`, when块试试always块的抽象代替.
  when(opcode === Opcode.R_TYPE || opcode === Opcode.OP_IMM) {
    alu_op := MuxLookup(funct3, ALUOp.ADD)(Seq(
      // R指令中, funct7(5)位决定是加法(0)/减法(1)
      "b000".U -> Mux(opcode === Opcode.R_TYPE && funct7(5), ALUOp.SUB, ALUOp.ADD),
      "b001".U -> ALUOp.SLL,
      "b010".U -> ALUOp.SLT,
      "b011".U -> ALUOp.SLTU,
      "b100".U -> ALUOp.XOR,
      //funct7(5)位决定是 逻辑(0)/算术(1) 右移
      "b101".U -> Mux(funct7(5), ALUOp.SRA, ALUOp.SRL),
      "b110".U -> ALUOp.OR,
      "b111".U -> ALUOp.AND
    ))
  }


  // 控制信号生成.通过opcode可以把指令分为9类. 它们是is_系列.
  //这些信号用来进一步生成控制信号
  val is_r_type  = opcode === Opcode.R_TYPE
  // RISC-V 官方叫 OP-IMM：addi/andi/ori/xori/slti/sltiu/slli/srli/srai
  // 注意：LOAD 也属于“RISC-V I-type 编码格式”，但它的 opcode 不等于 Opcode.OP_IMM
  val is_op_imm  = opcode === Opcode.OP_IMM
  val is_load    = opcode === Opcode.LOAD
  val is_store   = opcode === Opcode.STORE

  //下面的信号直接印出来
  val is_branch  = opcode === Opcode.BRANCH
  val is_jal     = opcode === Opcode.JAL
  val is_jalr    = opcode === Opcode.JALR
  val is_lui     = opcode === Opcode.LUI
  val is_auipc   = opcode === Opcode.AUIPC
  // val is_fence   = opcode === Opcode.FENCE // Fence 指令 (未实现)


/* 
output端口 信号连接
*/

  // output: 源寄存器读地址
  io.rs1_addr := rs1
  io.rs2_addr := rs2


  io.out.pc       := io.in.pc
  io.out.rs1_data := io.rs1_data
  io.out.rs2_data := io.rs2_data
  io.out.imm      := imm
  io.out.rd_addr  := rd

  // ALU控制信号
  io.out.alu_op   := alu_op
  // ALU 第二操作数选择：
  // - R-type: rs2
  // - Branch: 通常需要 rs2 参与比较（即使当前 EXU 里没有用 ALU 做比较，也建议保持语义正确）
  // - 其他（I/Load/Store/JALR 等）: imm
  io.out.alu_src  := !(is_r_type || is_branch)

  // 内存读写使能
  io.out.mem_wen  := is_store
  io.out.mem_ren  := is_load

  // 信号mem_op用来区分不同访存类型.
  io.out.mem_op   := funct3      // 传递 funct3 用于区分 lw/lbu/sw/sb


  //信号reg_wen表示是否是写回(WB)寄存器堆(rs1或rs2)的指令.
  // R-type: 显然要WB. op_imm: 显然要WB. load: 显然要WB
  // jal/jalr: 将PC+4存入rd.
  // lui: 把20bit的imm<<12后存入rd. auipc: 把20bit的imm<<12加pc后存入rd.
  io.out.reg_wen  := is_r_type || is_op_imm || is_load || is_jal || is_jalr || is_lui || is_auipc


  // 其他判断
  io.out.is_branch := is_branch
  io.out.branch_op := funct3    // 传递分支类型 (funct3)
  io.out.is_jal   := is_jal
  io.out.is_jalr  := is_jalr
  io.out.is_lui   := is_lui
  io.out.is_auipc := is_auipc
}




/*tips:

在 Chisel 中，只有满足以下任一条件时，信号才会被推断为 Reg (寄存器)：


Chisel语法             含义,                              结果
RegInit(...)          显式地实例化一个带初始值的寄存器。     Reg
Reg(...)              显式地实例化一个不带初始值的寄存器。   Reg
Mem(...)              实例化一个存储阵列（内存）            Reg 集合
ShiftRegister(...)    显式地创建一个移位寄存器。            Reg 集合







*/

