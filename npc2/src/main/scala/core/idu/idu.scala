// AzazeRV 译码单元 (Instruction Decode Unit)
package azazerv.idu

import chisel3._
import chisel3.util._
import azazerv._

/**
  * IDU - 译码单元
  *
  * 功能：
  * 1. 解析指令字段 (opcode, rd, rs1, rs2, funct3, funct7)
  * 2. 生成立即数 (I/S/B/U/J 类型)
  * 3. 读取寄存器堆
  * 4. 生成控制信号
  *
  * IDU 是纯组合逻辑: 当 io.in.valid 时, io.out.valid 立即拉高.
  * 反压: io.in.ready := io.out.ready (下游不接受则上游也不接受).
  */
class IDU extends Module {
  val io = IO(new Bundle {
    // ========== IFU->IDU ==========
    val in = Flipped(Decoupled(new IF2ID))  // 输入: IFU->IDU, 取到的(pc, inst)

    // ========== 读端口  IDU<->GPRFile  ==========
    val rs1_addr = Output(UInt(Config.REG_ADDR_W.W))  // 读地址1. IDU->GPRFile
    val rs2_addr = Output(UInt(Config.REG_ADDR_W.W))  // 读地址2. IDU->GPRFile
    val rs1_data = Input(UInt(Config.XLEN.W))         // 读数据1. GPRFile->IDU
    val rs2_data = Input(UInt(Config.XLEN.W))         // 读数据2. GPRFile->IDU

    // ========== IDU->EXU ==========
    val out = Decoupled(new ID2EX)  // 输出: IDU->EXU, 控制信号 + 操作数 + 立即数 + pc
  })

  val inst = io.in.bits.inst

  // 指令字段切片解析
  val opcode = inst(6, 0)
  val rd     = inst(11, 7)
  val funct3 = inst(14, 12)
  val rs1    = inst(19, 15)
  val rs2    = inst(24, 20)
  val funct7 = inst(31, 25)

  // 六种立即数生成
  val imm_i = Cat(Fill(20, inst(31)), inst(31, 20))
  val imm_s = Cat(Fill(20, inst(31)), inst(31, 25), inst(11, 7))
  val imm_b = Cat(Fill(19, inst(31)), inst(31), inst(7), inst(30, 25), inst(11, 8), 0.U(1.W))
  val imm_u = Cat(inst(31, 12), 0.U(12.W))
  val imm_j = Cat(Fill(11, inst(31)), inst(31), inst(19, 12), inst(20), inst(30, 21), 0.U(1.W))
  val zimm  = inst(19, 15)

  // 根据 opcode 选择imm
  val imm = MuxLookup(opcode, 0.U)(Seq(
    Opcode.OP_IMM -> imm_i,
    Opcode.LOAD   -> imm_i,
    Opcode.JALR   -> imm_i,
    Opcode.STORE  -> imm_s,
    Opcode.BRANCH -> imm_b,
    Opcode.LUI    -> imm_u,
    Opcode.AUIPC  -> imm_u,
    Opcode.JAL    -> imm_j,
    Opcode.SYSTEM -> Cat(0.U(27.W), zimm)
  ))

  // ALU 操作选择
  val alu_op = WireDefault(ALUOp.ADD)

  when(opcode === Opcode.R_TYPE || opcode === Opcode.OP_IMM) {
    alu_op := MuxLookup(funct3, ALUOp.ADD)(Seq(
      "b000".U -> Mux(opcode === Opcode.R_TYPE && funct7(5), ALUOp.SUB, ALUOp.ADD),
      "b001".U -> ALUOp.SLL,
      "b010".U -> ALUOp.SLT,
      "b011".U -> ALUOp.SLTU,
      "b100".U -> ALUOp.XOR,
      "b101".U -> Mux(funct7(5), ALUOp.SRA, ALUOp.SRL),
      "b110".U -> ALUOp.OR,
      "b111".U -> ALUOp.AND
    ))
  }

  // 指令分类信号
  val is_r_type  = opcode === Opcode.R_TYPE
  val is_op_imm  = opcode === Opcode.OP_IMM
  val is_load    = opcode === Opcode.LOAD
  val is_store   = opcode === Opcode.STORE
  val is_branch  = opcode === Opcode.BRANCH
  val is_jal     = opcode === Opcode.JAL
  val is_jalr    = opcode === Opcode.JALR
  val is_lui     = opcode === Opcode.LUI
  val is_auipc   = opcode === Opcode.AUIPC
  val is_system  = opcode === Opcode.SYSTEM

  val is_csr_inst = is_system && (funct3 =/= 0.U)
  val is_ecall    = is_system && (funct3 === 0.U) && (inst(31, 20) === 0.U)
  val is_ebreak   = is_system && (funct3 === 0.U) && (inst(31, 20) === 1.U)
  val is_mret     = is_system && (funct3 === 0.U) && (inst(31, 20) === 0x302.U)

  // ====================  GPR寄存器堆 ====================
  io.rs1_addr := rs1
  io.rs2_addr := rs2

  // ====================  输出端口 (via io.out.bits) ====================
  // 透传 inst_valid (PipeReg 冲刷时会标记为 false)
  io.out.bits.inst_valid := io.in.bits.inst_valid
  io.out.bits.pc       := io.in.bits.pc
  io.out.bits.inst     := io.in.bits.inst  // 透传原始指令
  io.out.bits.rs1_data := io.rs1_data
  io.out.bits.rs2_data := io.rs2_data
  io.out.bits.imm      := imm
  io.out.bits.rd_addr  := rd

  io.out.bits.alu_op   := alu_op
  io.out.bits.alu_src  := !(is_r_type || is_branch)

  io.out.bits.mem_wen  := is_store
  io.out.bits.mem_ren  := is_load
  io.out.bits.mem_op   := funct3

  io.out.bits.reg_wen  := is_r_type || is_op_imm || is_load || is_jal || is_jalr || is_lui || is_auipc || is_csr_inst

  io.out.bits.is_branch := is_branch
  io.out.bits.branch_op := funct3
  io.out.bits.is_jal    := is_jal
  io.out.bits.is_jalr   := is_jalr
  io.out.bits.is_lui    := is_lui
  io.out.bits.is_auipc  := is_auipc

  io.out.bits.is_csr   := is_csr_inst
  io.out.bits.csr_op   := funct3
  io.out.bits.csr_addr := inst(31, 20)
  io.out.bits.is_ecall := is_ecall
  io.out.bits.is_ebreak := is_ebreak
  io.out.bits.is_mret  := is_mret

  // ====================  握手逻辑 ====================
  // IDU 是纯组合逻辑: valid 直通, ready 直通 (反压)
  io.out.valid := io.in.valid
  io.in.ready  := io.out.ready
}
