// AzazeRV core共享定义
package azazerv

import chisel3._
import _root_.core.CoreConfig
import chisel3.util._


/**
  * RISC-V 指令操作码 (opcode) 定义
  * opcode 是指令的 [6:0] 位
  */
object Opcode {
  // R-type: 寄存器-寄存器运算
  val R_TYPE    = "b0110011".U(7.W)
  // I-type: 立即数运算 (OP-IMM)
  val OP_IMM    = "b0010011".U(7.W)
  val LOAD      = "b0000011".U(7.W)
  val JALR      = "b1100111".U(7.W)
  // S-type: Store
  val STORE     = "b0100011".U(7.W)
  // B-type: 分支
  val BRANCH    = "b1100011".U(7.W)
  // U-type: LUI, AUIPC
  val LUI       = "b0110111".U(7.W)
  val AUIPC     = "b0010111".U(7.W)
  // J-type: JAL
  val JAL       = "b1101111".U(7.W)

  // System: ecall, ebreak, 6*csr
  val SYSTEM    = "b1110011".U(7.W)
}

/**
  * CSR 操作类型 的 funct3 字段
  */
object CSROp {
  val RW  = "b001".U(3.W) //csrrw read/write
  val RS  = "b010".U(3.W) //csrrs read/set
  val RC  = "b011".U(3.W) //csrrc read/clear
  //使用zimm字段的csr指令
  val RWI = "b101".U(3.W) //csrrwi read/write immediate
  val RSI = "b110".U(3.W) //csrrsi read/set immediate
  val RCI = "b111".U(3.W) //csrrci read/clear immediate
}

/**
  * CSR 地址表
  */
object CSRAddr {
  val mstatus   = 0x300.U(12.W)
  val mtvec     = 0x305.U(12.W)
  val mepc      = 0x341.U(12.W)
  val mcause    = 0x342.U(12.W)
  val mcycle    = 0xB00.U(12.W)   // 周期计数器低32位
  val mcycleh   = 0xB80.U(12.W)   // 周期计数器高32位
  val mvendorid = 0xF11.U(12.W)   // 厂商标识 (只读)
  val marchid   = 0xF12.U(12.W)   // 架构标识 (只读)
}


object ALUFunction extends ChiselEnum {
  // 基础整数 ALU + M 扩展操作码。
  // 其中 M 扩展由 EXU 路由到 MDU 执行，ALU 本体只实现基础整数运算。
  val zero, add, sub, sll, slt, sltu, xor, srl, sra, or, and,
      mul, mulh, mulhsu, mulhu, div, divu, rem, remu = Value
}


// ALU 输入操作数选择
object ALUSrc1 extends ChiselEnum {
  val rs1, pc, zero = Value
}
object ALUSrc2 extends ChiselEnum {
  val rs2, imm, four = Value
}

/**
  * 分支操作类型 (funct3)
  */
object BranchOp {
  val BEQ  = "b000".U(3.W)  // 相等
  val BNE  = "b001".U(3.W)  // 不相等
  val BLT  = "b100".U(3.W)  // 有符号小于
  val BGE  = "b101".U(3.W)  // 有符号大于等于
  val BLTU = "b110".U(3.W)  // 无符号小于
  val BGEU = "b111".U(3.W)  // 无符号大于等于
}

/**
  * IFU -> IDU 接口
  * 当前的行为:
  * IFU不理解任何指令语义.
  */
class IF2ID(implicit val cfg: CoreConfig) extends Bundle {
  val pc   = UInt(cfg.addrBits.W)
  val inst = UInt(cfg.instBits.W)
  val inst_valid = Bool()  // 指令有效性标志 (flush 时置 false)
}

/**
* IDU -> EXU 接口.
* 当前的行为:
* IDU译码产生数据和控制信号, 后续流水线按需使用
*/
class ID2EX(implicit val cfg: CoreConfig) extends Bundle {

  // 透传IFU2IDU, 指令元信息
  val inst_valid = Bool()                 // 指令有效性标志 (flush 时置 false)
  val pc      = UInt(cfg.addrBits.W)
  val inst    = UInt(cfg.instBits.W) // 原始指令 (透传到WB级, 用于ebreak检测和debug)

  // 寄存器与操作数
  val rs1_data = UInt(cfg.dataBits.W)      // rs1 寄存器值
  val rs2_data = UInt(cfg.dataBits.W)      // rs2 寄存器值
  //val rs1_addr = UInt(cfg.regAddrBits.W)  // rs1 寄存器地址
  //val rs2_addr = UInt(cfg.regAddrBits.W)  // rs2 寄存器地址
  val imm     = UInt(cfg.dataBits.W)       // 立即数

  // 写回控制
  val rd_addr = UInt(cfg.regAddrBits.W) // 目标寄存器地址
  val reg_wen = Bool()                    // 寄存器写使能

  // ALU & MDU控制
  val alu_func = ALUFunction()            // ALU/MDU operation
  val alu_src1 = ALUSrc1()                // rs1 / pc / zero
  val alu_src2 = ALUSrc2()                // rs2 / imm / four

  // 访存控制
  val mem_wen = Bool()                    // 内存写使能, =is_store
  val mem_ren = Bool()                    // 内存读使能, =is_load
  val mem_op  = UInt(3.W)                 // 内存操作类型 (funct3: lw/lbu/sw/sb)

  // 控制流
  val is_branch = Bool()                  // 是否为分支指令
  val branch_op = UInt(3.W)               // 分支类型 (funct3: beq/bne/blt/bge/bltu/bgeu)
  val is_jal    = Bool()                  // 是否为 JAL
  val is_jalr   = Bool()                  // 是否为 JALR

  // CSR / System 信号
  val is_csr    = Bool()                  // 是否为 CSR 指令
  val csr_op    = UInt(3.W)               // CSR 操作类型 (funct3区分6个csr指令)
  val csr_addr  = UInt(12.W)              // CSR 地址. (csr指令属于I type, 其中csr段就是I type的imm)
  val is_ecall  = Bool()                  // 是否为 ECALL
  val is_ebreak = Bool()                  // 是否为 EBREAK
  val is_mret   = Bool()                  // 是否为 MRET
}

/**
  * EXU -> LSU 接口
  */
class EX2LS(implicit val cfg: CoreConfig) extends Bundle {
  val inst_valid = Bool()                     // 指令有效性标志 (flush 时置 false)
  val pc         = UInt(cfg.addrBits.W)  // 透传 PC (debug/ebreak)
  val inst       = UInt(cfg.instBits.W)  // 透传指令 (debug/ebreak)
  val alu_result = UInt(cfg.dataBits.W)    // ALU 计算结果
  val store_data = UInt(cfg.dataBits.W)    // Store 写入数据（来自 rs2，经旁路后透传到 MEM 阶段）
  val rd_addr    = UInt(cfg.regAddrBits.W)  // 目标寄存器地址
  val mem_wen    = Bool()
  val mem_ren    = Bool()
  val mem_op     = UInt(3.W)              // 内存操作类型 (funct3)
  val reg_wen    = Bool()
}

/**
  * LSU -> WBU 接口
  */
class LS2WB(implicit val cfg: CoreConfig) extends Bundle {
  val inst_valid = Bool()                   // 指令有效性标志 (flush 时置 false)
  val pc      = UInt(cfg.addrBits.W)   // 透传 PC (debug/ebreak)
  val inst    = UInt(cfg.instBits.W)   // 透传指令 (debug/ebreak)
  val wb_data = UInt(cfg.dataBits.W)         // 写回数据
  val rd_addr = UInt(cfg.regAddrBits.W)   // 写回寄存器地址
  val reg_wen = Bool()                      // 寄存器写使能
}

/**
  * DMEM AR 通道 (Read Address Channel)
  */
class DMemAR(implicit val cfg: CoreConfig) extends Bundle {
  val addr = UInt(cfg.addrBits.W)
}

/**
  * DMEM R 通道 (Read Data Channel)
  */
class DMemR(implicit val cfg: CoreConfig) extends Bundle {
  val data = UInt(cfg.dataBits.W)
}

/**
  * DMEM AW 通道 (Write Address Channel)
  */
class DMemAW(implicit val cfg: CoreConfig) extends Bundle {
  val addr = UInt(cfg.addrBits.W)
}

/**
  * DMEM W 通道 (Write Data Channel)
  */
class DMemW(implicit val cfg: CoreConfig) extends Bundle {
  val data = UInt(cfg.dataBits.W)
  val mask = UInt(4.W)
}

/**
  * DMEM B 通道 (Write Response Channel)
  */
class DMemB(implicit val cfg: CoreConfig) extends Bundle {
  val resp = UInt(2.W)  // 0 = OK
}

/**
  * 数据存储器接口 (AXI-Lite 风格: AR + R + AW + W + B)
  * 从 LSU 视角:
  *   - ar: LSU 发读地址 (Source)
  *   - r : LSU 收读数据 (Sink)
  *   - aw: LSU 发写地址 (Source)
  *   - w : LSU 发写数据 (Source)
  *   - b : LSU 收写响应 (Sink)
  */
class DMemIO(implicit val cfg: CoreConfig) extends Bundle {
  val ar = Decoupled(new DMemAR)
  val r  = Flipped(Decoupled(new DMemR))
  val aw = Decoupled(new DMemAW)
  val w  = Decoupled(new DMemW)
  val b  = Flipped(Decoupled(new DMemB))
}

/**
  * IMEM AR 通道 (Address Read Channel)
  * payload: addr, handshake: valid/ready
  */
class IMemAR(implicit val cfg: CoreConfig) extends Bundle {
  val addr = UInt(cfg.addrBits.W)
}

/**
  * IMEM R 通道 (Read Data Channel)
  * payload: data, handshake: valid/ready
  */
class IMemR(implicit val cfg: CoreConfig) extends Bundle {
  val data = UInt(cfg.instBits.W)
}

/**
  * 指令存储器接口 (AXI-Lite 风格: AR + R)
  * 从 IFU 视角：
  *   - ar: IFU 发地址 (Source)
  *   - r : IFU 收数据 (Sink)
  */
class IMemIO(implicit val cfg: CoreConfig) extends Bundle {
  val ar = Decoupled(new IMemAR)
  val r  = Flipped(Decoupled(new IMemR))
}

// 打拍寄存器
class PipeStage(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    val in  = Input(UInt(4.W))
    val out = Output(UInt(4.W))
  })

  // ① 定义一个寄存器，复位时值为 0，宽度为 4
  val piped_value = RegInit(0.U(4.W))

  // ② 使用非阻塞逻辑：将输入赋值给寄存器
  // Chisel 会自动在 always @(posedge clock) 块中生成非阻塞赋值 (<=)
  piped_value := io.in

  // ③ 将寄存器的值输出
  io.out := piped_value
}
// 结果：io.out 的值比 io.in 滞后一个时钟周期





