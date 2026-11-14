// AzazeRV 译码单元 (Instruction Decode Unit)
package azazerv.idu

import chisel3._
import _root_.core.CoreConfig
import chisel3.util._
import azazerv._

/**
  * IDU 输出控制信号：
  *
  * is_branch、is_jal、is_jalr
  * 由 opcode 生成，标记条件分支和两种跳转。
  * 由 EXU 消费.
  *
  * is_csr
  * 由 opcode == SYSTEM 且 funct3 != 000 生成，标记 CSR 指令。
  * 由 EXU 消费.
  *
  * is_ecall、is_ebreak、is_mret
  * 由 opcode == SYSTEM、funct3 == 000、inst(31, 20) 联合判断，末者分别为 0x000、0x001、0x302。
  * 由 EXU 消费.
  *
  * alu_func(ALUFunction)
  * 由 opcode、funct3、funct7 生成，选择整数或 M 扩展运算；M 扩展受 cfg.hasRVM 控制。
  * 由 EXU 内的 ALU / MDU 消费。
  *
  * alu_src1(ALUSrc1)、alu_src2(ALUSrc2)
  * 由 opcode 生成，选择 ALU 的两个输入来源。
  * 由 EXU 消费.
  *
  * mem_ren、mem_wen
  * 分别由 opcode == LOAD、opcode == STORE 生成，控制内存读写。
  * 由 LSU 消费.
  *
  * mem_size(MemSize)、load_unsigned
  * 由 opcode、funct3 生成，指定访存宽度和 Load 是否零扩展。
  * 由 LSU 消费.
  *
  * branch_op(BranchOp)
  * 直接取自 funct3，指定分支比较条件；is_branch 为 true 时使用。
  * 由 EXU 消费.
  *
  * csr_op(CSROp)
  * 直接取自 funct3，指定 CSR 操作及寄存器/立即数来源；is_csr 为 true 时生效。
  * 由 EXU 内的 CSRALU 消费.
  *
  * reg_wen
  * 由 opcode、funct3、funct7 和 cfg.hasRVM 生成，控制是否写回目标寄存器。
  * 经 EXU、LSU 透传，由 WBU 消费.
  */


// 1. ALU / MDU 运算控制码。
object ALUFunction extends ChiselEnum {
  val zero, add, sub, sll, slt, sltu, xor, srl, sra, or, and,
      mul, mulh, mulhsu, mulhu, div, divu, rem, remu = Value
}

// 2. 访存宽度；none 用于非访存指令及未支持的编码。
object MemSize extends ChiselEnum {
  val none, byte, half, word = Value
}

// 3. 分支比较控制码，沿用分支指令的 funct3 编码。
object BranchOp {
  val BEQ  = "b000".U(3.W)  // 相等
  val BNE  = "b001".U(3.W)  // 不相等
  val BLT  = "b100".U(3.W)  // 有符号小于
  val BGE  = "b101".U(3.W)  // 有符号大于等于
  val BLTU = "b110".U(3.W)  // 无符号小于
  val BGEU = "b111".U(3.W)  // 无符号大于等于
}

// 4. CSR 操作控制码，沿用 CSR 指令的 funct3 编码。
object CSROp {
  val RW  = "b001".U(3.W) // CSRRW：写入寄存器值
  val RS  = "b010".U(3.W) // CSRRS：按位设置
  val RC  = "b011".U(3.W) // CSRRC：按位清除
  val RWI = "b101".U(3.W) // CSRRWI：使用立即数
  val RSI = "b110".U(3.W) // CSRRSI：使用立即数
  val RCI = "b111".U(3.W) // CSRRCI：使用立即数
}

// ALU 输入来源由 opcode 决定，不属于上述 funct3 操作分类。
object ALUSrc1 extends ChiselEnum {
  val rs1, pc, zero = Value
}
object ALUSrc2 extends ChiselEnum {
  val rs2, imm, four = Value
}

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

  // IDU内部控制信号
  val is_r_type  = opcode === Opcode.R_TYPE
  val is_op_imm  = opcode === Opcode.OP_IMM
  val is_load    = opcode === Opcode.LOAD
  val is_store   = opcode === Opcode.STORE
  val is_lui     = opcode === Opcode.LUI
  val is_auipc   = opcode === Opcode.AUIPC
  val is_system  = opcode === Opcode.SYSTEM
  val is_m_encoding = is_r_type && (funct7 === "b0000001".U)


  // 输出控制信号
  val is_branch = opcode === Opcode.BRANCH
  val is_jal    = opcode === Opcode.JAL
  val is_jalr   = opcode === Opcode.JALR
  val is_csr    = is_system && (funct3 =/= 0.U)
  val is_ecall  = is_system && (funct3 === 0.U) && (inst(31, 20) === 0.U)
  val is_ebreak = is_system && (funct3 === 0.U) && (inst(31, 20) === 1.U)
  val is_mret   = is_system && (funct3 === 0.U) && (inst(31, 20) === 0x302.U)

  val alu_func = WireDefault(ALUFunction.add)
  val alu_src1 = WireDefault(ALUSrc1.rs1)
  val alu_src2 = WireDefault(ALUSrc2.imm)

  val mem_ren = is_load   //
  val mem_wen = is_store  //
  val mem_size = WireDefault(MemSize.none)
  val load_unsigned = WireDefault(false.B)

  val branch_op = funct3
  val csr_op    = funct3
  val reg_wen = (is_r_type && (!is_m_encoding || cfg.hasRVM.B)) ||
    is_op_imm || is_load || is_jal || is_jalr || is_lui || is_auipc || is_csr

  // 运算类型：先识别 M 扩展，再识别普通 R / OP_IMM 运算。
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
      "b000".U -> Mux(is_r_type && funct7(5), ALUFunction.sub, ALUFunction.add), // inst(30) 区分 ADD/SUB
      "b001".U -> ALUFunction.sll,
      "b010".U -> ALUFunction.slt,
      "b011".U -> ALUFunction.sltu,
      "b100".U -> ALUFunction.xor,
      "b101".U -> Mux(funct7(5), ALUFunction.sra, ALUFunction.srl), // inst(30) 区分逻辑/算术右移
      "b110".U -> ALUFunction.or,
      "b111".U -> ALUFunction.and
    ))
  }

  // ALU 输入来源
  when(is_lui) {
    alu_src1 := ALUSrc1.zero
  }.elsewhen(is_auipc || is_jal || is_jalr) {
    alu_src1 := ALUSrc1.pc
  }
  when(is_r_type || is_branch) {
    alu_src2 := ALUSrc2.rs2
  }.elsewhen(is_jal || is_jalr) {
    alu_src2 := ALUSrc2.four
  }

  // 访存宽度和 Load 扩展方式
  when(is_load || is_store) {
    switch(funct3) {
      is("b000".U) { mem_size := MemSize.byte } // LB / SB
      is("b001".U) { mem_size := MemSize.half } // LH / SH
      is("b010".U) { mem_size := MemSize.word } // LW / SW
      is("b100".U) {
        when(is_load) { // LBU
          mem_size := MemSize.byte
          load_unsigned := true.B
        }
      }
      is("b101".U) {
        when(is_load) { // LHU
          mem_size := MemSize.half
          load_unsigned := true.B
        }
      }
    }
  }

  // ================= 接口连接：只赋值，不在这里译码 =================
  // 寄存器堆读口与数据。
  io.rs1_addr := rs1
  io.rs2_addr := rs2
  io.out.bits.rs1_data := io.rs1_data
  io.out.bits.rs2_data := io.rs2_data

  // 指令信息、立即数与目标地址。
  io.out.bits.inst_valid := io.in.bits.inst_valid
  io.out.bits.pc       := io.in.bits.pc
  io.out.bits.inst     := inst
  io.out.bits.imm      := imm
  io.out.bits.rd_addr  := rd
  io.out.bits.csr_addr := inst(31, 20)

  // 输出控制信号。
  io.out.bits.alu_func := alu_func
  io.out.bits.alu_src1 := alu_src1
  io.out.bits.alu_src2 := alu_src2
  io.out.bits.mem_ren  := mem_ren
  io.out.bits.mem_wen  := mem_wen
  io.out.bits.mem_size := mem_size
  io.out.bits.load_unsigned := load_unsigned
  io.out.bits.reg_wen  := reg_wen

  // 控制流：分支条件类型与跳转指令标志
  io.out.bits.is_branch := is_branch
  io.out.bits.branch_op := branch_op
  io.out.bits.is_jal    := is_jal
  io.out.bits.is_jalr   := is_jalr

  // CSR/系统指令：CSR 操作与异常返回等控制信号
  io.out.bits.is_csr   := is_csr
  io.out.bits.csr_op   := csr_op
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
