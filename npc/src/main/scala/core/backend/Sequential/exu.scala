// AzazeRV 执行单元 (Execution Unit)
package azazerv.exu

import chisel3._
import _root_.core.{CoreConfig, ALU, MDU}
import chisel3.util._
import azazerv._

/**
  * EXU - 执行单元
  *
  * 功能：
  * 1. ALU MDU运算
  * 2. 分支/跳转地址计算
  * 3. 分支条件判断
  *
  * Integer/CSR instructions pass through combinationally; M instructions stall until MDU responds.
  * The upstream PipeReg holds the instruction until io.in.fire; MDU requests issue only once.
  * Redirects and CSR side effects occur only when the instruction leaves EXU.
  */
class EXU(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    // ========== IDU->EXU ==========
    val in = Flipped(Decoupled(new ID2EX))

    // ========== EXU->LSU ==========
    val out = Decoupled(new EX2LS)

    // ========== EXU->IFU (控制冒险重定向) ==========
    val jump_en   = Output(Bool())
    val jump_addr = Output(UInt(cfg.addrBits.W))

    // ========== EXU<->CSRFile ==========
    val csr_rdata = Input(UInt(cfg.dataBits.W))
    val csr_wdata = Output(UInt(cfg.dataBits.W))
    val csr_addr  = Output(UInt(12.W))
    val csr_wen   = Output(Bool())
    val is_ecall  = Output(Bool())
    val is_ebreak = Output(Bool())
    val is_mret   = Output(Bool())
    val mtvec     = Input(UInt(cfg.dataBits.W))
    val mepc      = Input(UInt(cfg.dataBits.W))
  })

  val in = io.in.bits

  // ALU 操作数
  val alu_a = MuxLookup(in.alu_src1, in.rs1_data)(Seq(
    ALUSrc1.pc -> in.pc,
    ALUSrc1.zero -> 0.U(cfg.dataBits.W)
  ))
  val alu_b = MuxLookup(in.alu_src2, in.rs2_data)(Seq(
    ALUSrc2.imm -> in.imm,
    ALUSrc2.four -> 4.U(cfg.dataBits.W)
  ))

  // CSR ALU 子模块
  val csralu = Module(new CSRALU)
  csralu.io.csr_op    := in.csr_op
  csralu.io.rs1_data  := in.rs1_data
  csralu.io.imm       := in.imm
  csralu.io.csr_rdata := io.csr_rdata

  io.csr_wdata := csralu.io.csr_wdata
  io.csr_addr  := in.csr_addr
  // CSR 写使能: 有效指令离开 EXU 时只触发一次
  io.csr_wen   := in.is_csr && io.out.fire && in.inst_valid
  io.is_ecall  := in.is_ecall && io.out.fire && in.inst_valid
  io.is_ebreak := in.is_ebreak && io.out.fire && in.inst_valid
  io.is_mret   := in.is_mret && io.out.fire && in.inst_valid

  val alu = Module(new ALU)
  alu.io.func := in.alu_func
  alu.io.op1 := alu_a
  alu.io.op2 := alu_b

  val mdu_result = WireDefault(0.U(cfg.dataBits.W))
  val mdu_valid = WireDefault(false.B)
  val is_mdu = in.inst_valid && (
    in.alu_func === ALUFunction.mul || in.alu_func === ALUFunction.mulh ||
    in.alu_func === ALUFunction.mulhsu || in.alu_func === ALUFunction.mulhu ||
    in.alu_func === ALUFunction.div || in.alu_func === ALUFunction.divu ||
    in.alu_func === ALUFunction.rem || in.alu_func === ALUFunction.remu)

  if (cfg.hasRVM) {
    val mdu = Module(new MDU)
    val mdu_started = RegInit(false.B)
    mdu.io.func := in.alu_func
    mdu.io.op1 := alu_a
    mdu.io.op2 := alu_b
    mdu.io.reqValid := io.in.valid && is_mdu && !mdu_started
    mdu.io.respReady := io.out.ready && io.in.valid && is_mdu
    when(mdu.io.reqValid && mdu.io.reqReady) { mdu_started := true.B }
    when(mdu.io.respValid && mdu.io.respReady) { mdu_started := false.B }
    mdu_result := mdu.io.result
    mdu_valid := mdu.io.respValid
  }

  // LUI=0+imm, AUIPC=PC+imm, JAL/JALR link=PC+4 are ordinary ALU additions.
  val execute_result = Mux(is_mdu, mdu_result, alu.io.result)
  val final_alu_result = Mux(in.is_csr, io.csr_rdata, execute_result)
  val pc_plus_imm = in.pc + in.imm

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

  // Redirect once, when a valid instruction leaves EXU.
  io.jump_en := io.out.fire && in.inst_valid && (in.is_jal || in.is_jalr || (in.is_branch && branch_taken) || in.is_ecall || in.is_ebreak || in.is_mret)
  io.jump_addr := MuxCase(pc_plus_imm, Seq(
    in.is_jalr   -> ((in.rs1_data + in.imm) & ~1.U(cfg.addrBits.W)),
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
  val result_ready = !is_mdu || mdu_valid
  io.out.valid := io.in.valid && result_ready
  io.in.ready  := io.out.ready && result_ready
}
