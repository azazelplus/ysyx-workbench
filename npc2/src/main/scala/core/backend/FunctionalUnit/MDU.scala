package core

import chisel3._
import chisel3.util._

/** MDU - RV32M 乘除法功能单元。
  *
  * 设计原则：
  *   - 参考 NutShell 的 FU 拆分风格, 把 M 扩展从 ALU 中独立出来. 避免把重算术逻辑放在 ALU 组合路径上，减轻关键路径压力. 
  *
  * 实现策略：
  *   - MUL*：可配置快乘路径（默认开启），并保留迭代乘法后备实现。
  *   - DIV/REM*：恢复法迭代除法，结合最高位对齐减少空迭代。
  */
class MDU(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    // 请求握手
    val reqValid = Input(Bool())      
    val reqReady = Output(Bool())     

    // 请求内容
    val func = Input(ALUFunctions())  // 操作类型 mul/mulh/mulhsu/mulhu/div/rem
    val op1 = Input(UInt(cfg.dataWidth))  
    val op2 = Input(UInt(cfg.dataWidth))

    // 响应握手
    val respValid = Output(Bool())
    val respReady = Input(Bool())

    // 响应内容
    val result = Output(UInt(cfg.dataWidth))
  })

  import ALUFunctions._

  private val xlen = cfg.dataBits

  private def neg(x: UInt): UInt = (~x).asUInt + 1.U

  val sIdle :: sMulFast1 :: sMulIter :: sDiv :: sResp :: Nil =
    Enum(5)
  val state = RegInit(sIdle)

  io.reqReady := state === sIdle
  io.respValid := state === sResp

  val funcReg = Reg(ALUFunctions())
  val resultReg = Reg(UInt(xlen.W))
  io.result := resultReg

  val reqFire = io.reqValid && io.reqReady
  val respFire = io.respValid && io.respReady

  // 快乘路径状态寄存器
  val mulFastAReg = Reg(UInt(xlen.W))
  val mulFastBReg = Reg(UInt(xlen.W))
  val mulFastNegRes = Reg(Bool())
  val mulFastHighRes = Reg(Bool())

  // 迭代乘法状态寄存器
  val mulAcc = Reg(UInt((2 * xlen).W))
  val mulMcand = Reg(UInt((2 * xlen).W))
  val mulMplier = Reg(UInt(xlen.W))
  val mulCnt = Reg(UInt(6.W))
  val mulNegRes = Reg(Bool())
  val mulHighRes = Reg(Bool())

  // 迭代除法状态寄存器（按最高位对齐 early-out）
  val divNum = Reg(UInt(xlen.W))
  val divDenShift = Reg(UInt((2 * xlen).W))
  val divQuot = Reg(UInt(xlen.W))
  val divStep = Reg(UInt(log2Ceil(xlen).W))
  val divNegQ = Reg(Bool())
  val divNegR = Reg(Bool())
  val divReturnRem = Reg(Bool())

  val isMulOp = (io.func === mul) || (io.func === mulh) ||
    (io.func === mulhsu) || (io.func === mulhu)
  val isDivOp = (io.func === div) || (io.func === divu) ||
    (io.func === rem) || (io.func === remu)

  when(reqFire) {
    funcReg := io.func
    when(isMulOp) {
      val signedA = (io.func === mul) || (io.func === mulh) || (io.func === mulhsu)
      val signedB = (io.func === mul) || (io.func === mulh)

      val aNeg = signedA && io.op1(xlen - 1)
      val bNeg = signedB && io.op2(xlen - 1)
      val aAbs = Mux(aNeg, neg(io.op1), io.op1)
      val bAbs = Mux(bNeg, neg(io.op2), io.op2)

      mulAcc := 0.U
      mulMcand := Cat(0.U(xlen.W), aAbs)
      mulMplier := bAbs
      mulCnt := 0.U
      mulNegRes := aNeg ^ bNeg
      mulHighRes := io.func =/= mul
      if (cfg.hasRVM) {
        mulFastAReg := aAbs
        mulFastBReg := bAbs
        mulFastNegRes := aNeg ^ bNeg
        mulFastHighRes := io.func =/= mul
        state := sMulFast1
      } else {
        state := sMulIter
      }
    }.elsewhen(isDivOp) {
      val signedOp = (io.func === div) || (io.func === rem)
      val returnRem = (io.func === rem) || (io.func === remu)

      val aNeg = signedOp && io.op1(xlen - 1)
      val bNeg = signedOp && io.op2(xlen - 1)
      val aAbs = Mux(aNeg, neg(io.op1), io.op1)
      val bAbs = Mux(bNeg, neg(io.op2), io.op2)

      val divByZero = io.op2 === 0.U
      val signedMin = (1.U << (xlen - 1)).asUInt
      val signedNeg1 = Fill(xlen, 1.U(1.W))
      val divOverflow = signedOp && (io.op1 === signedMin) && (io.op2 === signedNeg1)

      when(divByZero) {
        resultReg := Mux(returnRem, io.op1, Fill(xlen, 1.U(1.W)))
        state := sResp
      }.elsewhen(divOverflow) {
        resultReg := Mux(returnRem, 0.U, signedMin)
        state := sResp
      }.elsewhen(aAbs < bAbs) {
        val rSigned = Mux(aNeg, neg(aAbs), aAbs)
        resultReg := Mux(returnRem, rSigned, 0.U(xlen.W))
        state := sResp
      }.otherwise {
        val shift = Log2(aAbs) - Log2(bAbs)
        divNum := aAbs
        divDenShift := Cat(0.U(xlen.W), bAbs) << shift
        divQuot := 0.U
        divStep := shift
        divNegQ := aNeg ^ bNeg
        divNegR := aNeg
        divReturnRem := returnRem
        state := sDiv
      }
    }.otherwise {
      resultReg := 0.U
      state := sResp
    }
  }

  when(state === sMulFast1) {
    // 快乘路径：在同一拍完成乘积、符号修正与高低位选择。
    val rawProd = mulFastAReg * mulFastBReg
    val finalProd = Mux(mulFastNegRes, neg(rawProd), rawProd)
    resultReg := Mux(
      mulFastHighRes,
      finalProd(2 * xlen - 1, xlen),
      finalProd(xlen - 1, 0)
    )
    state := sResp
  }

  when(state === sMulIter) {
    val addend = Mux(mulMplier(0), mulMcand, 0.U((2 * xlen).W))
    val nextAcc = mulAcc + addend

    mulAcc := nextAcc
    mulMcand := mulMcand << 1
    mulMplier := mulMplier >> 1
    mulCnt := mulCnt + 1.U

    when(mulCnt === (xlen - 1).U) {
      val finalProd = Mux(mulNegRes, neg(nextAcc), nextAcc)
      resultReg := Mux(mulHighRes, finalProd(2 * xlen - 1, xlen), finalProd(xlen - 1, 0))
      state := sResp
    }
  }

  when(state === sDiv) {
    val numExt = Cat(0.U(xlen.W), divNum)
    val canSub = numExt >= divDenShift
    val nextNumExt = Mux(canSub, numExt - divDenShift, numExt)
    val bitMask = 1.U(xlen.W) << divStep
    val nextQuot = Mux(canSub, divQuot | bitMask, divQuot)

    divNum := nextNumExt(xlen - 1, 0)
    divQuot := nextQuot
    divDenShift := divDenShift >> 1

    when(divStep === 0.U) {
      val qUnsigned = nextQuot
      val rUnsigned = nextNumExt(xlen - 1, 0)
      val qSigned = Mux(divNegQ, neg(qUnsigned), qUnsigned)
      val rSigned = Mux(divNegR, neg(rUnsigned), rUnsigned)
      resultReg := Mux(divReturnRem, rSigned, qSigned)
      state := sResp
    }.otherwise {
      divStep := divStep - 1.U
    }
  }

  when(respFire) {
    state := sIdle
  }
}
