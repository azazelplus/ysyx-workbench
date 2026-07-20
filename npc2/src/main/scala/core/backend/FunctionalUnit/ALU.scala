package core

import chisel3._
import chisel3.util._

object ALUFunctions extends ChiselEnum {
  // 基础整数 ALU + M 扩展操作码。
  // 其中 M 扩展由 EXU 路由到 MDU 执行，ALU 本体只实现基础整数运算。
  val zero, add, sub, sll, slt, sltu, xor, srl, sra, or, and,
      mul, mulh, mulhsu, mulhu, div, divu, rem, remu = Value
}

class ALU(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    val func = Input(ALUFunctions())
    val op1 = Input(UInt(cfg.dataWidth))
    val op2 = Input(UInt(cfg.dataWidth))
    val result = Output(UInt(cfg.dataWidth))
  })

  import ALUFunctions._
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

// TODO: ALUControl 需要等待指令定义文件整合完成
// class ALUControl(implicit val cfg: CoreConfig) extends Module {
//   val io = IO(new Bundle {
//     val opcode = Input(UInt(7.W))
//     val funct3 = Input(UInt(3.W))
//     val funct7 = Input(UInt(7.W))
//     val aluFunc = Output(ALUFunctions())
//   })
//
//   import ALUFunctions._
//   io.aluFunc := zero
//
//   switch(io.opcode) {
//     is(InstructionTypes.I) {
//       io.aluFunc := MuxLookup(io.funct3, zero)(
//         Seq(
//           InstructionsTypeI.addi -> add,
//           InstructionsTypeI.slli -> sll,
//           InstructionsTypeI.slti -> slt,
//           InstructionsTypeI.sltiu -> sltu,
//           InstructionsTypeI.xori -> xor,
//           InstructionsTypeI.sri -> Mux(io.funct7(5), sra, srl),
//           InstructionsTypeI.ori -> or,
//           InstructionsTypeI.andi -> and
//         )
//       )
//     }
//     is(InstructionTypes.RM) {
//       // R-type 指令在 funct7=0000001 且 M 扩展开启时，译码到 MDU 操作码。
//       val isMInst = cfg.hasRVM.B && (io.funct7 === RVMInstructions.funct7)
//       io.aluFunc := Mux(
//         isMInst,
//         MuxLookup(io.funct3, zero)(
//           Seq(
//             RVMFunct3.mul -> mul,
//             RVMFunct3.mulh -> mulh,
//             RVMFunct3.mulhsu -> mulhsu,
//             RVMFunct3.mulhu -> mulhu,
//             RVMFunct3.div -> div,
//             RVMFunct3.divu -> divu,
//             RVMFunct3.rem -> rem,
//             RVMFunct3.remu -> remu
//           )
//         ),
//         MuxLookup(io.funct3, zero)(
//           Seq(
//             InstructionsTypeR.add_sub -> Mux(io.funct7(5), sub, add),
//             InstructionsTypeR.sll -> sll,
//             InstructionsTypeR.slt -> slt,
//             InstructionsTypeR.sltu -> sltu,
//             InstructionsTypeR.xor -> xor,
//             InstructionsTypeR.sr -> Mux(io.funct7(5), sra, srl),
//             InstructionsTypeR.or -> or,
//             InstructionsTypeR.and -> and
//           )
//         )
//       )
//     }
//     is(InstructionTypes.B) { io.aluFunc := add }
//     is(InstructionTypes.L) { io.aluFunc := add }
//     is(InstructionTypes.S) { io.aluFunc := add }
//     is(Instructions.JAL) { io.aluFunc := add }
//     is(Instructions.JALR) { io.aluFunc := add }
//     is(Instructions.LUI) { io.aluFunc := add }
//     is(Instructions.AUIPC) { io.aluFunc := add }
//   }
// }
