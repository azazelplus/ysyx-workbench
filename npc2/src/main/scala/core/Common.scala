// MiniRV 共享定义
package minirv

import chisel3._
import chisel3.util._

/**
  * MiniRV 配置常量
  */
object Config {
  val XLEN = 32          // eXecution length 数据宽度 (RV32) npc2是32位cpu.
  val ADDR_WIDTH = 32    // 地址宽度
  val INST_WIDTH = 32    // 指令宽度
  val REG_ADDR_W = 5     // 寄存器地址宽度 (32个寄存器)
}

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
  * CSR 操作类型 (funct3)
  */
object CSROp {
  val RW  = "b001".U(3.W)
  val RS  = "b010".U(3.W)
  val RC  = "b011".U(3.W)
  val RWI = "b101".U(3.W)
  val RSI = "b110".U(3.W)
  val RCI = "b111".U(3.W)
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

/**
  * ALU 操作码
  */
object ALUOp {
  val ADD  = 0.U(4.W)
  val SUB  = 1.U(4.W)
  val AND  = 2.U(4.W)
  val OR   = 3.U(4.W)
  val XOR  = 4.U(4.W)
  val SLL  = 5.U(4.W)   // shift left logic 逻辑左移
  val SRL  = 6.U(4.W)   // shift right logic 逻辑右移
  val SRA  = 7.U(4.W)   // shift right arithmetic 算术右移
  val SLT  = 8.U(4.W)   // set less than 有符号比较
  val SLTU = 9.U(4.W)   // set less than unsigned 无符号比较
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
  */
class IF2ID extends Bundle {
  val pc   = UInt(Config.ADDR_WIDTH.W)
  val inst = UInt(Config.INST_WIDTH.W)
  val inst_valid = Bool()  // 指令有效性标志 (flush 时置 false)
}

/**
  * IDU -> EXU 接口
  */
class ID2EX extends Bundle {
  val inst_valid = Bool()                 // 指令有效性标志 (flush 时置 false)
  val pc      = UInt(Config.ADDR_WIDTH.W)
  val inst    = UInt(Config.INST_WIDTH.W) // 原始指令 (透传到WB级, 用于ebreak检测和debug)
  val rs1_data = UInt(Config.XLEN.W)      // rs1 寄存器值
  val rs2_data = UInt(Config.XLEN.W)      // rs2 寄存器值
  val imm     = UInt(Config.XLEN.W)       // 立即数
  val rd_addr = UInt(Config.REG_ADDR_W.W) // 目标寄存器地址
  val alu_op  = UInt(4.W)                 // ALU 操作码
  val alu_src = Bool()                    // ALU 第二操作数选择 (0: rs2, 1: imm)
  val mem_wen = Bool()                    // 内存写使能, =is_store
  val mem_ren = Bool()                    // 内存读使能, =is_load
  val mem_op  = UInt(3.W)                 // 内存操作类型 (funct3: lw/lbu/sw/sb)
  val reg_wen = Bool()                    // 寄存器写使能
  val is_branch = Bool()                  // 是否为分支指令
  val branch_op = UInt(3.W)               // 分支类型 (funct3: beq/bne/blt/bge/bltu/bgeu)
  val is_jal    = Bool()                  // 是否为 JAL
  val is_jalr   = Bool()                  // 是否为 JALR
  val is_lui    = Bool()                  // 是否为 LUI
  val is_auipc  = Bool()                  // 是否为 AUIPC

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
class EX2LS extends Bundle {
  val inst_valid = Bool()                     // 指令有效性标志 (flush 时置 false)
  val pc         = UInt(Config.ADDR_WIDTH.W)  // 透传 PC (debug/ebreak)
  val inst       = UInt(Config.INST_WIDTH.W)  // 透传指令 (debug/ebreak)
  val alu_result = UInt(Config.XLEN.W)    // ALU 计算结果
  val store_data = UInt(Config.XLEN.W)    // Store 写入数据（来自 rs2，经旁路后透传到 MEM 阶段）
  val rd_addr    = UInt(Config.REG_ADDR_W.W)  // 目标寄存器地址
  val mem_wen    = Bool()
  val mem_ren    = Bool()
  val mem_op     = UInt(3.W)              // 内存操作类型 (funct3)
  val reg_wen    = Bool()
}

/**
  * LSU -> WBU 接口
  */
class LS2WB extends Bundle {
  val inst_valid = Bool()                   // 指令有效性标志 (flush 时置 false)
  val pc      = UInt(Config.ADDR_WIDTH.W)   // 透传 PC (debug/ebreak)
  val inst    = UInt(Config.INST_WIDTH.W)   // 透传指令 (debug/ebreak)
  val wb_data = UInt(Config.XLEN.W)         // 写回数据
  val rd_addr = UInt(Config.REG_ADDR_W.W)   // 写回寄存器地址
  val reg_wen = Bool()                      // 寄存器写使能
}

/**
  * DMEM AR 通道 (Read Address Channel)
  */
class DMemAR extends Bundle {
  val addr = UInt(Config.ADDR_WIDTH.W)
}

/**
  * DMEM R 通道 (Read Data Channel)
  */
class DMemR extends Bundle {
  val data = UInt(Config.XLEN.W)
}

/**
  * DMEM AW 通道 (Write Address Channel)
  */
class DMemAW extends Bundle {
  val addr = UInt(Config.ADDR_WIDTH.W)
}

/**
  * DMEM W 通道 (Write Data Channel)
  */
class DMemW extends Bundle {
  val data = UInt(Config.XLEN.W)
  val mask = UInt(4.W)
}

/**
  * DMEM B 通道 (Write Response Channel)
  */
class DMemB extends Bundle {
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
class DMemIO extends Bundle {
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
class IMemAR extends Bundle {
  val addr = UInt(Config.ADDR_WIDTH.W)
}

/**
  * IMEM R 通道 (Read Data Channel)
  * payload: data, handshake: valid/ready
  */
class IMemR extends Bundle {
  val data = UInt(Config.INST_WIDTH.W)
}

/**
  * 指令存储器接口 (AXI-Lite 风格: AR + R)
  * 从 IFU 视角：
  *   - ar: IFU 发地址 (Source)
  *   - r : IFU 收数据 (Sink)
  */
class IMemIO extends Bundle {
  val ar = Decoupled(new IMemAR)
  val r  = Flipped(Decoupled(new IMemR))
}

// 打拍寄存器
class PipeStage extends Module {
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





