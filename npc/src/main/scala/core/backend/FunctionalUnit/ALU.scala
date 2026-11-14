package core

import chisel3._
import chisel3.util._
import azazerv.idu.ALUFunction

class ALU(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    val func = Input(ALUFunction())
    val op1 = Input(UInt(cfg.dataWidth))
    val op2 = Input(UInt(cfg.dataWidth))
    val result = Output(UInt(cfg.dataWidth))
  })

  import ALUFunction._
  io.result := 0.U
  switch(io.func) {
    is(add) { io.result := io.op1 + io.op2 }
    is(sub) { io.result := io.op1 - io.op2 }
    is(sll) { io.result := io.op1 << io.op2(4, 0) }
    is(slt) { io.result := (io.op1.asSInt < io.op2.asSInt).asUInt }
    is(sltu) { io.result := (io.op1 < io.op2).asUInt }
    is(xor) { io.result := io.op1 ^ io.op2 }
    is(srl) { io.result := io.op1 >> io.op2(4, 0) }
    is(sra) { io.result := (io.op1.asSInt >> io.op2(4, 0)).asUInt }
    is(or) { io.result := io.op1 | io.op2 }
    is(and) { io.result := io.op1 & io.op2 }
  }
}
