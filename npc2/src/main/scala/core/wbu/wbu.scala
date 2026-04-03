// MiniRV 写回单元 (Write Back Unit)
package minirv.wbu

import chisel3._
import chisel3.util._
import minirv._

/**
  * WBU - 写回单元 (pipeline drain)
  *
  * 功能: 将结果写回寄存器堆.
  * WBU 始终 ready (GPR 写是瞬时的), 仅在 io.in.valid 时实际写 GPR.
  */
class WBU extends Module {
  val io = IO(new Bundle {
    // 来自 LSU
    val in = Flipped(Decoupled(new LS2WB))

    // 寄存器堆写端口
    val rd_addr = Output(UInt(Config.REG_ADDR_W.W))
    val rd_data = Output(UInt(Config.XLEN.W))
    val rd_wen  = Output(Bool())

    // debug / ebreak 输出
    val debug_pc   = Output(UInt(Config.ADDR_WIDTH.W))
    val debug_inst = Output(UInt(Config.INST_WIDTH.W))
    val inst_valid = Output(Bool())  // 本周期有有效指令到达 WB 级
  })

  // WBU 始终 ready
  io.in.ready := true.B

  // 仅在 valid 且 inst_valid 时写 GPR (无效指令不写)
  io.rd_addr := io.in.bits.rd_addr
  io.rd_data := io.in.bits.wb_data
  io.rd_wen  := io.in.bits.reg_wen && io.in.valid && io.in.bits.inst_valid

  // debug / ebreak
  io.debug_pc   := io.in.bits.pc
  io.debug_inst := io.in.bits.inst
  // inst_valid: 仅当有真正有效的指令提交时为 true
  io.inst_valid := io.in.valid && io.in.bits.inst_valid
}
