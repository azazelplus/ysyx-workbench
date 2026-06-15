// AzazeRV 执行单元 (Execution Unit)
package azazerv.exu

import chisel3._
import chisel3.util._
import azazerv._

/**
  * EXU - 执行单元
  *
  * 功能：
  * 1. ALU 运算
  * 2. 分支/跳转地址计算
  * 3. 分支条件判断
  *
  * EXU 是纯组合逻辑: 当 io.in.valid 时, io.out.valid 立即拉高.
  * jump_en/jump_addr 仅在 io.in.valid 时有效.
  */
class EXU extends Module {
  val io = IO(new Bundle {
    // ========== IDU->EXU ==========
    val in = Flipped(Decoupled(new ID2EX))

    // ========== EXU->LSU ==========
    val out = Decoupled(new EX2LS)

    // ========== EXU->IFU (控制冒险重定向) ==========
    val jump_en   = Output(Bool())
    val jump_addr = Output(UInt(Config.ADDR_WIDTH.W))

    // ========== EXU<->CSRFile ==========
    val csr_rdata = Input(UInt(Config.XLEN.W))
    val csr_wdata = Output(UInt(Config.XLEN.W))
    val csr_addr  = Output(UInt(12.W))
    val csr_wen   = Output(Bool())
    val is_ecall  = Output(Bool())
    val is_ebreak = Output(Bool())
    val is_mret   = Output(Bool())
    val mtvec     = Input(UInt(Config.XLEN.W))
    val mepc      = Input(UInt(Config.XLEN.W))
  })

  val in = io.in.bits

  // ALU 操作数
  val alu_a = in.rs1_data
  val alu_b = Mux(in.alu_src, in.imm, in.rs2_data)

  // CSR ALU 子模块
  val csralu = Module(new CSRALU)
  csralu.io.csr_op    := in.csr_op
  csralu.io.rs1_data  := in.rs1_data
  csralu.io.imm       := in.imm
  csralu.io.csr_rdata := io.csr_rdata

  io.csr_wdata := csralu.io.csr_wdata
  io.csr_addr  := in.csr_addr
  // CSR 写使能: 仅在有效指令时才写 (inst_valid=true 且 in.valid)
  io.csr_wen   := in.is_csr && io.in.valid && in.inst_valid
  io.is_ecall  := in.is_ecall && io.in.valid && in.inst_valid
  io.is_ebreak := in.is_ebreak && io.in.valid && in.inst_valid
  io.is_mret   := in.is_mret && io.in.valid && in.inst_valid

  // ALU 计算
  val alu_result = WireDefault(0.U(Config.XLEN.W))

  switch(in.alu_op) {
    is(ALUOp.ADD)  { alu_result := alu_a + alu_b }
    is(ALUOp.SUB)  { alu_result := alu_a - alu_b }
    is(ALUOp.AND)  { alu_result := alu_a & alu_b }
    is(ALUOp.OR)   { alu_result := alu_a | alu_b }
    is(ALUOp.XOR)  { alu_result := alu_a ^ alu_b }
    is(ALUOp.SLL)  { alu_result := alu_a << alu_b(4, 0) }
    is(ALUOp.SRL)  { alu_result := alu_a >> alu_b(4, 0) }
    is(ALUOp.SRA)  { alu_result := (alu_a.asSInt >> alu_b(4, 0)).asUInt }
    is(ALUOp.SLT)  { alu_result := (alu_a.asSInt < alu_b.asSInt).asUInt }
    is(ALUOp.SLTU) { alu_result := (alu_a < alu_b).asUInt }
  }

  // Utype 特殊处理
  val lui_result   = in.imm
  val pc_plus_imm  = in.pc + in.imm

  // 最终 ALU 结果选择
  val final_alu_result = MuxCase(alu_result, Seq(
    in.is_lui   -> lui_result,
    in.is_auipc -> pc_plus_imm,
    (in.is_jal || in.is_jalr) -> (in.pc + 4.U),
    in.is_csr   -> io.csr_rdata
  ))

  // 分支条件判断
  val rs1_s = in.rs1_data.asSInt
  val rs2_s = in.rs2_data.asSInt
  val branch_taken = WireDefault(false.B)
  when(in.is_branch) {
    switch(in.branch_op) {
      is(BranchOp.BEQ)  { branch_taken := in.rs1_data === in.rs2_data }
      is(BranchOp.BNE)  { branch_taken := in.rs1_data =/= in.rs2_data }
      is(BranchOp.BLT)  { branch_taken := rs1_s < rs2_s }
      is(BranchOp.BGE)  { branch_taken := rs1_s >= rs2_s }
      is(BranchOp.BLTU) { branch_taken := in.rs1_data < in.rs2_data }
      is(BranchOp.BGEU) { branch_taken := in.rs1_data >= in.rs2_data }
    }
  }

  // 跳转控制 (仅在 valid 且 inst_valid 时有效, 避免无效指令产生跳转)
  io.jump_en := io.in.valid && in.inst_valid && (in.is_jal || in.is_jalr || (in.is_branch && branch_taken) || in.is_ecall || in.is_ebreak || in.is_mret)
  io.jump_addr := MuxCase(pc_plus_imm, Seq(
    in.is_jalr   -> ((in.rs1_data + in.imm) & ~1.U(Config.ADDR_WIDTH.W)),
    in.is_ecall  -> io.mtvec,
    in.is_ebreak -> io.mtvec,
    in.is_mret   -> io.mepc
  ))

  // 输出到 LSU (via io.out.bits)
  io.out.bits.inst_valid := in.inst_valid  // 透传 inst_valid
  io.out.bits.pc         := in.pc
  io.out.bits.inst       := in.inst
  io.out.bits.alu_result := final_alu_result
  io.out.bits.store_data := in.rs2_data
  io.out.bits.rd_addr    := in.rd_addr
  io.out.bits.mem_wen    := in.mem_wen
  io.out.bits.mem_ren    := in.mem_ren
  io.out.bits.mem_op     := in.mem_op
  io.out.bits.reg_wen    := in.reg_wen

  // ====================  握手逻辑 ====================
  io.out.valid := io.in.valid
  io.in.ready  := io.out.ready
}
