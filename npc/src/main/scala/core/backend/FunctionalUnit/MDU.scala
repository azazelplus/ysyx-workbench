package core

import chisel3._
import chisel3.util._
import azazerv.ALUFunction

/** MDU - RV32M 乘除法功能单元
  * 设计原则：
  *   - 参考 NutShell 的 FU 拆分风格, 把 M 扩展从 ALU 中独立出来. 避免把重算术逻辑放在 ALU 组合路径上，减轻关键路径压力
  * 单请求, 非流水化, 由FSM控制的多周期运算单元
  * 快速乘法由chisel `*`运算符直接生成组合逻辑实现.
  *   - MUL*：可配置快乘路径（默认开启），并保留迭代乘法后备实现。
  *   - DIV/REM*：恢复法迭代除法，结合最高位对齐减少空迭代。
  */
class MDU(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    // 请求握手
    val reqValid = Input(Bool())      
    val reqReady = Output(Bool())     
    // 请求内容
    val func = Input(ALUFunction())  // 操作类型 mul/mulh/mulhsu/mulhu/div/rem
    val op1 = Input(UInt(cfg.dataWidth))  
    val op2 = Input(UInt(cfg.dataWidth))

    // 响应握手
    val respValid = Output(Bool())
    val respReady = Input(Bool())
    // 响应内容
    val result = Output(UInt(cfg.dataWidth))
  })

  import ALUFunction._  // 成员引入当前作用域...所以 class MDU 内可以直接写 mul、div、rem，不用每次写 ALUFunction.mul

  private val xlen = cfg.dataBits

  // 辅助函数neg, 取负数.
  private def neg(x: UInt): UInt = (~x).asUInt + 1.U

  // MDU状态机: sIdle空闲接收新请求, sMulFast1执行快乘, sMulIter执行迭代乘法, sDiv执行迭代除法, sResp计算完成等待下游接收. sIdle -> sMulFast1/sMulIter/sDiv -> sResp -> sIdle...  
  val sIdle :: sMulFast1 :: sMulIter :: sDiv :: sResp :: Nil =
    Enum(5)
  val state = RegInit(sIdle)

  // 运算完成时锁存结果，在 sResp 等待下游接收期间保持稳定。
  // io.result 只是连接寄存器输出，不会再增加一拍。
  val resultReg = Reg(UInt(xlen.W)) 
  io.result := resultReg

  // 握手
  io.reqReady := state === sIdle  // 空闲时可以接收请求
  io.respValid := state === sResp // 计算完成时响应有效
  val reqFire = io.reqValid && io.reqReady
  val respFire = io.respValid && io.respReady

  // 快乘路径状态寄存器: 两个乘数, 符号标志, 高低位选择标志
  val mulFastAReg = Reg(UInt(xlen.W)) // 乘数a的绝对值
  val mulFastBReg = Reg(UInt(xlen.W)) //乘数b的绝对值
  val mulFastNegRes = Reg(Bool())     // 结果是否为负数
  val mulFastHighRes = Reg(Bool())    // 存储高位结果标志, 除了mul返回低32位, mulh/mulhsu/mulhu都返回高32位.

  // 迭代乘法状态寄存器
  val mulAcc = Reg(UInt((2 * xlen).W))      // 部分积 累加器.  存储当前累加的部分积
  val mulMcand = Reg(UInt((2 * xlen).W))    // 本拍被加数. 它最初存的是被乘数|op1|, 每个周期左移一位. 这样就带上了当前要乘的位(即mulMplier的LSB)的位权重了, 可以直接加到最终结果上.
  val mulMplier = Reg(UInt(xlen.W))         // 还没处理的乘数位. 它最初存的是乘数|op2|, 每个周期右移一位并取LSB, 实现遍历乘数每一位.
  val mulCnt = Reg(UInt(log2Ceil(xlen).W))  // 迭代计数器. 还有多少位没处理
  val mulNegRes = Reg(Bool())               // 结果符号, 和快乘的 mulFastNegRes 一样.最终结果是否需要取负
  val mulHighRes = Reg(Bool())              // 存储高位结果标志, 和快乘的 mulFastHighRes 一样. 

  // 迭代除法状态寄存器（按最高位对齐 early-out）
  val divNum = Reg(UInt(xlen.W))            	// 当前部分余数. 初始为被除数|op1|, 每轮尝试减去移位后的除数.
  val divDenShift = Reg(UInt((2 * xlen).W)) 	// 移位后的除数绝对值. 初始按最高位对齐被除数, 每轮右移一位.
  val divQuot = Reg(UInt(xlen.W))           	// 已求出的商(无符号). 初始为0, 当前余数够减时将对应商位置1.
  val divStep = Reg(UInt(log2Ceil(xlen).W)) 	// 迭代计数器. 当前要计算的商位下标. 逐拍递减, 处理完第0位后结束迭代.
  val divNegQ = Reg(Bool())                 	// 商是否需要取负.
  val divNegR = Reg(Bool())                 	// 余数是否需要取负. 有符号余数的符号跟随被除数.
  val divReturnRem = Reg(Bool())            	// 结果选择标志. rem/remu返回余数, div/divu返回商.

  // 乘法/除法判断
  val isMulOp = (io.func === mul) || (io.func === mulh) ||
    (io.func === mulhsu) || (io.func === mulhu)
  val isDivOp = (io.func === div) || (io.func === divu) ||
    (io.func === rem) || (io.func === remu)

  // 计算寄存器初始化. 考虑除法的特殊情况: `除零`,`溢出`, `结果为0的快速除法`
  when(reqFire) {
    //乘法指令
    when(isMulOp) {
      // 操作数a, b是否为有符号
      val signedA = (io.func === mul) || (io.func === mulh) || (io.func === mulhsu) 
      val signedB = (io.func === mul) || (io.func === mulh) 
      // 操作数a, b是否为负数(有符号数, 且MSB=1)
      val aNeg = signedA && io.op1(xlen - 1)
      val bNeg = signedB && io.op2(xlen - 1)
      // 操作数a, b的绝对值
      val aAbs = Mux(aNeg, neg(io.op1), io.op1)
      val bAbs = Mux(bNeg, neg(io.op2), io.op2)

      // 寄存器初始化.
      if (cfg.fastMul) {
        // 初始化快乘路径状态寄存器。
        mulFastAReg := aAbs 
        mulFastBReg := bAbs
        mulFastNegRes := aNeg ^ bNeg  // 存储结果的符号
        mulFastHighRes := io.func =/= mul // 存储高位结果标志
        state := sMulFast1  // 进入快乘路径状态机
      } else {
        // 初始化迭代乘法状态寄存器.
        mulAcc := 0.U   //部分积初始化为0, 2*32bit
        mulMcand := Cat(0.U(xlen.W), aAbs)  // mcand a的绝对值, 扩展至2*32bit方便左移.
        mulMplier := bAbs                   // mplier b的绝对值
        mulCnt := 0.U                       // 迭代计数器初始化为0. 到31就结束迭代
        mulNegRes := aNeg ^ bNeg            // a*b的结果是否为负数
        mulHighRes := io.func =/= mul       // 除了mul, mulh/mulhsu/mulhu都返回高32位.
        state := sMulIter // 进入迭代乘法状态机
      }
    }.elsewhen(isDivOp)   // 除法指令
    {
      val signedOp = (io.func === div) || (io.func === rem)   // 是否是有符号除法(div, rem), (无符号: divu, remu)
      val returnRem = (io.func === rem) || (io.func === remu) // 取余(rem, remu)还是 除法(div, divu)

      // 操作数a, b是否为负数(有符号数, 且MSB=1)
      val aNeg = signedOp && io.op1(xlen - 1)
      val bNeg = signedOp && io.op2(xlen - 1)
      // 操作数a, b的绝对值
      val aAbs = Mux(aNeg, neg(io.op1), io.op1)
      val bAbs = Mux(bNeg, neg(io.op2), io.op2)

      // 除零和有符号溢出检测
      val divByZero = io.op2 === 0.U  // 除数为0
      val signedMin = (1.U << (xlen - 1)).asUInt  // 32bit有符号最小值, 即0x80000000
      val signedNeg1 = Fill(xlen, 1.U(1.W))       // 32bit有符号-1, 即0xFFFFFFFF
      val divOverflow = signedOp && (io.op1 === signedMin) && (io.op2 === signedNeg1) // 有符号除法溢出: 被除数为最小值, 除数为-1
      when(divByZero) {
        resultReg := Mux(returnRem, io.op1, Fill(xlen, 1.U(1.W))) // RISCV要求: 除零时, div返回所有位 置`1`, rem返回被除数op1.
        state := sResp
      }.elsewhen(divOverflow) {
        resultReg := Mux(returnRem, 0.U, signedMin) //RISCV要求: 溢出时, div返回原始被除数op1=signedMin, rem返回0.
        state := sResp
      }.elsewhen(aAbs < bAbs) { // 快速路径: aAbs < bAbs时 商为0余数为被除数, 不需要进入迭代除法.
        val rSigned = Mux(aNeg, neg(aAbs), aAbs)  // rem符号
        resultReg := Mux(returnRem, rSigned, 0.U(xlen.W))
        state := sResp
      }.otherwise { // 迭代除法的初始化.
        val shift = Log2(aAbs) - Log2(bAbs) // 除数 要左移多少位才可以和 被除数 对齐.
        divNum := aAbs    // 被除数
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
  // 快乘路径：在同一拍完成无符号乘积, 加符号, 高低位选择.
  if (cfg.fastMul) {
    when(state === sMulFast1) {
      val rawProd = mulFastAReg * mulFastBReg // 绝对值乘积结果. 使用chisel `*` 运算符, 综合工具就会推断出一个组合逻辑乘法器结构.
      val finalProd = Mux(mulFastNegRes, neg(rawProd), rawProd) // 对负结果加负号
      // 高低位选择, 得到最终结果
      resultReg := Mux(
        mulFastHighRes,
        finalProd(2 * xlen - 1, xlen),
        finalProd(xlen - 1, 0)
      )
      state := sResp
    }
  } else {
    // 迭代乘法路径
    when(state === sMulIter) {
      val addend = Mux(mulMplier(0), mulMcand, 0.U((2 * xlen).W)) // 取乘数LSB和被乘数相乘.
      val nextAcc = mulAcc + addend   // 累加后的部分积

      mulAcc := nextAcc           // 部分积寄存器更新
      mulMcand := mulMcand << 1   // 被乘数左移一位(*2), 权重高一位.
      mulMplier := mulMplier >> 1 // 乘数右移一位, 挤掉处理过的LSB.
      mulCnt := mulCnt + 1.U      // 迭代计数器加1

      //结束迭代
      when(mulCnt === (xlen - 1).U) {
        val finalProd = Mux(mulNegRes, neg(nextAcc), nextAcc) // 添加负号
        resultReg := Mux(mulHighRes, finalProd(2 * xlen - 1, xlen), finalProd(xlen - 1, 0)) // 选择高位,得到最终结果
        state := sResp
      }
    }
  }
  // 迭代除法路径
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














/* 
注: 迭代除法是恢复余数法（restoring division）的移位减法版本。它把除数左移到和被除数最高位对齐，然后每拍右移一位，逐位试减，能减就在商对应位置 1，不能减就保持余数。shift 用来跳过不可能产生商 1 的高位，减少迭代次数。
回忆十进制竖式除法, 一开始就是把除数放大到和被除数最高位对齐, 进行第一次试除. 原理是一样的.
 */