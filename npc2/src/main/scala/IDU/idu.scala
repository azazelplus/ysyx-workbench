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
    val in = Flipped(Decoupled(new IF2ID))  // 输入: IFU->IDU, 取到的(pc, inst)

    // ========== 读端口  IDU<->GPRFile  ==========
    val rs1_addr = Output(UInt(Config.REG_ADDR_W.W))  // 读地址1. IDU->GPRFile
    val rs2_addr = Output(UInt(Config.REG_ADDR_W.W))  // 读地址2. IDU->GPRFile
    val rs1_data = Input(UInt(Config.XLEN.W))         // 读数据1. GPRFile->IDU（顶层已做前递/旁路）
    val rs2_data = Input(UInt(Config.XLEN.W))         // 读数据2. GPRFile->IDU（顶层已做前递/旁路）

    // ========== IDU->EXU ==========
    val out = Output(new ID2EX)  // 输出: IDU->EXU, 控制信号 + 操作数 + 立即数 + pc
  })

  val inst = io.in.bits.inst  //bits是Decoupled的预设信号, 即要IFU->IDU的数据, 一个IF2ID实例

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
  val zimm  = inst(19, 15) // csr指令的立即数, 位置和 rs1 相同! 

  // 根据 opcode 选择imm. riscv根据imm在指令中位置不同来分类出6个type, 而所有指令末尾7bit的opcode区分出type, 进而切割出imm.
  val imm = MuxLookup(opcode, 0.U)(Seq(
    Opcode.OP_IMM -> imm_i,
    Opcode.LOAD   -> imm_i,
    Opcode.JALR   -> imm_i,
    Opcode.STORE  -> imm_s,
    Opcode.BRANCH -> imm_b,
    Opcode.LUI    -> imm_u,
    Opcode.AUIPC  -> imm_u,
    Opcode.JAL    -> imm_j,
    Opcode.SYSTEM -> Cat(0.U(27.W), zimm) // for CSRRWI/CSRRSI/CSRRCI (zimm)
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

/***************************************************
* 生成指令分类信号. 通过opcode可以把指令分为9类. 它们是is_系列. 这些信号用来进一步生成控制信号
*/
  val is_r_type  = opcode === Opcode.R_TYPE
  // OP-IMM类包括:addi/andi/ori/xori/slti/sltiu/slli/srli/srai
  val is_op_imm  = opcode === Opcode.OP_IMM
  val is_load    = opcode === Opcode.LOAD
  val is_store   = opcode === Opcode.STORE

  val is_branch  = opcode === Opcode.BRANCH
  val is_jal     = opcode === Opcode.JAL
  val is_jalr    = opcode === Opcode.JALR
  val is_lui     = opcode === Opcode.LUI
  val is_auipc   = opcode === Opcode.AUIPC
  val is_system  = opcode === Opcode.SYSTEM

  // 生成是否为System指令(ecall, ebreak, 6*csr)信号.
  val is_csr_inst = is_system && (funct3 =/= 0.U)
  val is_ecall    = is_system && (funct3 === 0.U) && (inst(31, 20) === 0.U)
  val is_ebreak   = is_system && (funct3 === 0.U) && (inst(31, 20) === 1.U)
  val is_mret     = is_system && (funct3 === 0.U) && (inst(31, 20) === 0x302.U)




/** ============================================================================
IDU的output端口信号连接 - 按功能模块分类
=============================================================================== */

  // ====================  GPR寄存器堆 ====================
  io.rs1_addr := rs1  //源寄存器地址1
  io.rs2_addr := rs2  //源寄存器地址2

  // ====================  数据通路 (PC/操作数/立即数/目标寄存器) ====================
  io.out.pc       := io.in.bits.pc
  io.out.rs1_data := io.rs1_data
  io.out.rs2_data := io.rs2_data
  io.out.imm      := imm
  io.out.rd_addr  := rd

  // ====================  ALU 控制 ====================
  io.out.alu_op   := alu_op
  // ALU 第二操作数选择：
  //   - R-type: rs2（两个寄存器运算）
  //   - Branch: rs2（用于条件比较）
  //   - 其他（I/Load/Store/JALR 等）: imm（立即数运算）
  io.out.alu_src  := !(is_r_type || is_branch)

  // ====================  内存访问控制 ====================
  io.out.mem_wen  := is_store   // 写使能
  io.out.mem_ren  := is_load    // 读使能
  io.out.mem_op   := funct3     // 操作类型 (区分 lw/lbu/sw/sb 等)

  // ====================  GPR寄存器写回 ====================
  // 需要写回 rd 的指令：R-type, OP-IMM, LOAD, JAL, JALR, LUI, AUIPC, CSR
  io.out.reg_wen  := is_r_type || is_op_imm || is_load || is_jal || is_jalr || is_lui || is_auipc || is_csr_inst

  // ====================  分支/跳转控制 ====================
  io.out.is_branch := is_branch
  io.out.branch_op := funct3      // 分支类型 (funct3: beq/bne/blt/bge/bltu/bgeu)
  io.out.is_jal    := is_jal
  io.out.is_jalr   := is_jalr
  io.out.is_lui    := is_lui
  io.out.is_auipc  := is_auipc

  // ====================  系统指令控制 (异常/CSR) ====================
  io.out.is_csr   := is_csr_inst
  io.out.csr_op   := funct3       // CSR 操作类型 (funct3: 6种CSR指令)
  io.out.csr_addr := inst(31, 20) // CSR寄存器地址 (I-type 的立即数字段)
  io.out.is_ecall := is_ecall
  io.out.is_ebreak := is_ebreak
  io.out.is_mret  := is_mret


  /***************************和IDU的握手逻辑*****************************/
  // IDU 状态机: 状态是 {valid, decode_done}，输出 ready
  // 与 IFU 状态机对称:
  //   IFU: {fetch_done, ready} → 输出 valid
  //   IDU: {valid,       decode_done} → 输出 ready
  val decode_done = true.B  // 单周期: 译码立即完成; 未来多周期: 替换为实际完成信号

  val s_wait_valid :: s_busy :: Nil = Enum(2)
  // s_wait_valid: 空闲等待指令, ready=1
  // s_busy:       正在译码,     ready=0
  val state = RegInit(s_wait_valid)

  switch(state) {
    is(s_wait_valid) {
      when(io.in.fire) {    // 握手完成 (valid && ready): 收到有效指令, 开始译码
        state := s_busy
      }
    }
    is(s_busy) {
      when(decode_done) {   // 译码完成 → 重新等待
        state := s_wait_valid
      }
    }
  }
  // ready 由状态驱动(输出), 不依赖 valid (避免组合环)
  io.in.ready := (state === s_wait_valid)
}









/*tips:

在 Chisel 中，只有满足以下任一条件时，信号才会被推断为 Reg (寄存器)：


Chisel语法             含义,                              结果
RegInit(...)          显式地实例化一个带初始值的寄存器。     Reg
Reg(...)              显式地实例化一个不带初始值的寄存器。   Reg
Mem(...)              实例化一个存储阵列（内存）            Reg 集合
ShiftRegister(...)    显式地创建一个移位寄存器。            Reg 集合


MuxLookup生成查找表多路选择器. 它是组合逻辑块.
完整签名: MuxLookup(key: UInt, default: T)(mapping: Seq[(UInt, T)]): T
MuxLookup这个allpy函数调用时跟了两个括号, 这是[currying]柯里化特性. 就是把函数的多个参数用多个有序括号给出. 这样好处是提高可读性 和 帮助编译器类型推断.
其中, default是默认[值]. 键值对 匹配失败则返回这个默认[值].
其中Seq中的元素类型是(UInt, T), 一个一个元组. 元组的第一个元素key必须是UInt类型, 比如`Opcode.OP_IMM`. 第二个元素是值, 类型T(任意).
-> 符号是创建元组的语法糖, A -> B 等同于 (A, B), 仅仅易读这是个键值对.




*/

