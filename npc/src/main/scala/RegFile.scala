// MiniRV 寄存器堆
package minirv

import chisel3._
import chisel3.util._

/**
  * 32 个通用寄存器
  * x0 恒为 0
  */
class RegFile extends Module {
  val io = IO(new Bundle {
    // 两个读端口 (rs1, rs2)
    val rs1_addr = Input(UInt(Config.REG_ADDR_W.W))
    val rs2_addr = Input(UInt(Config.REG_ADDR_W.W))
    val rs1_data = Output(UInt(Config.XLEN.W))
    val rs2_data = Output(UInt(Config.XLEN.W))
    
    // 一个写端口 (rd)
    val rd_addr  = Input(UInt(Config.REG_ADDR_W.W))
    val rd_data  = Input(UInt(Config.XLEN.W))
    val rd_wen   = Input(Bool())
  })

  // 32 个 32 位寄存器.
  val regs = RegInit(VecInit(Seq.fill(32)(0.U(Config.XLEN.W))))

  // 读取 (x0 恒为 0)
  io.rs1_data := Mux(io.rs1_addr === 0.U, 0.U, regs(io.rs1_addr))
  io.rs2_data := Mux(io.rs2_addr === 0.U, 0.U, regs(io.rs2_addr))

  // 写入 (x0 不可写)
  when(io.rd_wen && io.rd_addr =/= 0.U) {
    regs(io.rd_addr) := io.rd_data
  }
}
