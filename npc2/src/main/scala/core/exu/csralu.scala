package azazerv.exu

import chisel3._
import chisel3.util._
import azazerv._

/**
  * CSR ALU - 负责 CSR 读写数据的位运算逻辑
  */
class CSRALU extends Module {
  val io = IO(new Bundle {
    val csr_op    = Input(UInt(3.W))               // CSR 操作码 (funct3)
    val rs1_data  = Input(UInt(Config.XLEN.W))     // rs1
    val imm       = Input(UInt(Config.XLEN.W))     // zimm (已扩展, 来自 IDU 的 imm 端口)
    val csr_rdata = Input(UInt(Config.XLEN.W))     // 从 CSR 寄存器堆读回的旧值

    val csr_wdata = Output(UInt(Config.XLEN.W))    // 计算出的写入数据
  })

  // 判断csr指令否是立即数版本 (funct3[2] == 1)
  // 001(csrrw), 010(csrrs), 011(csrrc) -> Reg
  // 101(csrrwi), 110(csrrsi), 111(csrrci) -> Imm
  val is_imm_op = io.csr_op(2)

  // 操作数 1 选择: 立即数 zimm 或 寄存器值 rs1
  val op1 = Mux(is_imm_op, io.imm, io.rs1_data)

  // 计算写入数据
  // CSRRW / CSRRWI: wdata = op1
  // CSRRS / CSRRSI: wdata = old_csr | op1
  // CSRRC / CSRRCI: wdata = old_csr & ~op1
  io.csr_wdata := MuxLookup(io.csr_op, 0.U)(Seq(
    CSROp.RW  -> op1,
    CSROp.RWI -> op1,
    CSROp.RS  -> (io.csr_rdata | op1),
    CSROp.RSI -> (io.csr_rdata | op1),
    CSROp.RC  -> (io.csr_rdata & ~op1),
    CSROp.RCI -> (io.csr_rdata & ~op1)
  ))
}
