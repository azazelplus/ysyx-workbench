// MiniRV 写回单元 (Write Back Unit)
package minirv.wbu

import chisel3._
import chisel3.util._
import minirv._

/**
  * WBU - 写回单元
  * 
  * 功能：
  * 1. 将结果写回寄存器堆
  */
class WBU extends Module {
  val io = IO(new Bundle {
    // 来自 LSU
    val in = Input(new LS2WB)
    
    // 寄存器堆写端口
    val rd_addr = Output(UInt(Config.REG_ADDR_W.W))
    val rd_data = Output(UInt(Config.XLEN.W))
    val rd_wen  = Output(Bool())
  })

  // 直接传递写回信号
  io.rd_addr := io.in.rd_addr
  io.rd_data := io.in.wb_data
  io.rd_wen  := io.in.reg_wen
}
