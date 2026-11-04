// AzazeRV 译码单元 (Instruction Decode Unit)
package azazerv.idu

import chisel3._
import _root_.core.CoreConfig
import chisel3.util._
import azazerv._

/**
  * IDU - 译码单元
  *
  * 功能：
  * 1. 解析指令字段 (opcode, rd, rs1, rs2, funct3, funct7)
  * 2. 生成立即数 (I/S/B/U/J 类型)
  * 3. 读取寄存器堆
  * 4. 生成整个流水线的控制信号. 会随着对应指令一起通过各级流水线寄存器传播，最终在正确的阶段被消耗...
  *
  * IDU 是纯组合逻辑: 当 io.in.valid 时, io.out.valid 立即拉高.
  * 反压: io.in.ready := io.out.ready (下游不接受则上游也不接受).
  */
class IDU(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    // IFU->IDU
    val in = Flipped(Decoupled(new IF2ID))  // 输入: IFU->IDU, 取到的(pc, inst)

    // 2*GPR读端口  IDU<->GPRFile
    val rs1_addr = Output(UInt(cfg.regAddrBits.W))  // 读地址1. IDU->GPRFile
    val rs2_addr = Output(UInt(cfg.regAddrBits.W))  // 读地址2. IDU->GPRFile
    val rs1_data = Input(UInt(cfg.dataBits.W))         // 读数据1. GPRFile->IDU
    val rs2_data = Input(UInt(cfg.dataBits.W))         // 读数据2. GPRFile->IDU

    // IDU->EXU
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
  val zimm  = inst(19, 15)  // 5-bit zero-extended immediate for CSR instructions

  // 根据 opcode 分类指令, 选择imm
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

  // 生成alu_func信号, 供exu选择调用alu或mdu.
  val is_m_encoding = (opcode === Opcode.R_TYPE) && (funct7 === "b0000001".U)  // 是否是 M 扩展的 R 指令
  // 先识别M指令, 再识别普通R/I算数逻辑运算
  val alu_func = WireDefault(ALUFunction.add) //  默认为ALU add运算
  when(is_m_encoding) {
    alu_func := ALUFunction.zero  // 占位操作码. 不会被使用
    if (cfg.hasRVM) {
      alu_func := MuxLookup(funct3, ALUFunction.zero)(Seq(
        "b000".U -> ALUFunction.mul,
        "b001".U -> ALUFunction.mulh,
        "b010".U -> ALUFunction.mulhsu,
        "b011".U -> ALUFunction.mulhu,
        "b100".U -> ALUFunction.div,
        "b101".U -> ALUFunction.divu,
        "b110".U -> ALUFunction.rem,
        "b111".U -> ALUFunction.remu
      ))
    }
  }.elsewhen(opcode === Opcode.R_TYPE || opcode === Opcode.OP_IMM) {
    alu_func := MuxLookup(funct3, ALUFunction.add)(Seq(
      "b000".U -> Mux(opcode === Opcode.R_TYPE && funct7(5), ALUFunction.sub, ALUFunction.add),
      "b001".U -> ALUFunction.sll,
      "b010".U -> ALUFunction.slt,
      "b011".U -> ALUFunction.sltu,
      "b100".U -> ALUFunction.xor,
      "b101".U -> Mux(funct7(5), ALUFunction.sra, ALUFunction.srl),
      "b110".U -> ALUFunction.or,
      "b111".U -> ALUFunction.and
    ))
  }

  // 指令分类信号. 分为opcode区分出的RV32I的 11大类 + SYSTEM类的4个具体指令
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



  // ================= 接口连接 =================

  // 寄存器堆读口：输出 rs1/rs2 地址，读回的数据随指令送往 EXU
  io.rs1_addr := rs1
  io.rs2_addr := rs2
  io.out.bits.rs1_data := io.rs1_data
  io.out.bits.rs2_data := io.rs2_data


  // 指令信息：透传有效位、PC 和原始指令
  io.out.bits.inst_valid := io.in.bits.inst_valid // (PipeReg 冲刷时会标记为 false)
  io.out.bits.pc       := io.in.bits.pc
  io.out.bits.inst     := io.in.bits.inst  // 透传原始指令

  // 译码结果：立即数和目标寄存器号
  io.out.bits.imm      := imm
  io.out.bits.rd_addr  := rd

  // 运算控制：指定 ALU/MDU 操作及两个 ALU 操作数的来源
  io.out.bits.alu_func := alu_func
  io.out.bits.alu_src1 := MuxCase(ALUSrc1.rs1, Seq(
    is_lui -> ALUSrc1.zero,
    (is_auipc || is_jal || is_jalr) -> ALUSrc1.pc
  ))
  io.out.bits.alu_src2 := MuxCase(ALUSrc2.imm, Seq(
    (is_r_type || is_branch) -> ALUSrc2.rs2,
    (is_jal || is_jalr) -> ALUSrc2.four
  ))

  // 访存控制：读写使能及 load/store 的 funct3
  io.out.bits.mem_wen  := is_store
  io.out.bits.mem_ren  := is_load
  io.out.bits.mem_op   := funct3

  // 写回控制：需要写 rd 的指令才使能；关闭 M 扩展时不写回 M 指令
  io.out.bits.reg_wen  := (is_r_type && (!is_m_encoding || cfg.hasRVM.B)) || is_op_imm || is_load || is_jal || is_jalr || is_lui || is_auipc || is_csr_inst

  // 控制流：分支条件类型与跳转指令标志
  io.out.bits.is_branch := is_branch
  io.out.bits.branch_op := funct3
  io.out.bits.is_jal    := is_jal
  io.out.bits.is_jalr   := is_jalr

  // CSR / 系统指令：CSR 操作与异常返回等控制信号
  io.out.bits.is_csr   := is_csr_inst
  io.out.bits.csr_op   := funct3
  io.out.bits.csr_addr := inst(31, 20)
  io.out.bits.is_ecall := is_ecall
  io.out.bits.is_ebreak := is_ebreak
  io.out.bits.is_mret  := is_mret

  // 断言: 未打开hasRVM时, 不允许出现M扩展指令.
  if (!cfg.hasRVM) {
    when(io.out.fire && io.in.bits.inst_valid) {
      assert(!is_m_encoding, "RV32M instruction encountered with hasRVM=false")
    }
  }

  // 握手逻辑
  // IDU 是纯组合逻辑: valid 直通, ready 直通 (反压)
  io.out.valid := io.in.valid
  io.in.ready  := io.out.ready
}
