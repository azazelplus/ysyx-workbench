// AzazeRV 流水线寄存器 (Pipeline Register with inst_valid flush semantics)
package azazerv

import chisel3._
import chisel3.util._
import chisel3.experimental.requireIsChiselType // 这个 import 是为了使用 requireIsChiselType 来检查 gen 的类型

/**
  * PipeReg - 支持 inst_valid 冲刷语义的流水线寄存器
  *
  * 与普通 Queue 的差异：
  *   - flush 拉高时不清空队列
  *   - 将队列中元素的 inst_valid 置为 false
  *   - 入队时检查 inst_valid，无效指令直接丢弃（不进入队列）
  *
  * 约束：数据类型必须包含 inst_valid: Bool 字段
  *
  * @param flushAffectsOutput 是否让 flush 影响当前输出
  *   - true (默认): flush 时当前输出也被标记无效
  *   - false: flush 只影响队列内部条目，当前输出不受影响
  *   用于避免 EXU 处跳转指令时的组合环路
  */
class PipeRegIO[T <: Data](private val gen: T, val entries: Int) extends Bundle {
  val enq   = Flipped(Decoupled(gen))
  val deq   = Decoupled(gen)
  val count = Output(UInt(log2Ceil(entries + 1).W))
  val flush = Input(Bool())
}

class PipeReg[T <: Data](
    val gen: T,
    val entries: Int = 1,
    val pipe: Boolean = false,        // 当 deq.ready 时允许同周期写入
    val flow: Boolean = false,        // 空时直接透传（组合路径）
    val flushAffectsOutput: Boolean = true  // flush 是否影响当前输出
) extends Module {
  require(entries > 0, "PipeReg entries must be > 0")
  requireIsChiselType(gen)

  // 检查 gen 是否包含 inst_valid 字段
  private val genRecord = gen match {
    case r: Record => r
    case _ =>
      throw new IllegalArgumentException(
        "PipeReg requires Bundle type containing 'inst_valid: Bool' field"
      )
  }
  require(
    genRecord.elements.contains("inst_valid"),
    "PipeReg payload must contain field 'inst_valid'"
  )

  val io = IO(new PipeRegIO(gen, entries))

  // 辅助函数：获取/设置 inst_valid 字段
  private def getInstValid(x: T): Bool =
    x.asInstanceOf[Record].elements("inst_valid").asInstanceOf[Bool]

  private def withForcedInvalid(x: T, forceInvalid: Bool): T = {
    val out = Wire(chiselTypeOf(x))
    out := x
    when(forceInvalid) {
      out.asInstanceOf[Record].elements("inst_valid").asInstanceOf[Bool] := false.B
    }
    out
  }

  // 队列存储
  val ram = Mem(entries, gen)

  // 每个槽位的 invalid 标记（flush 时整体置位）
  val invalidMask = RegInit(VecInit(Seq.fill(entries)(false.B)))

  // 指针和状态
  val enqPtr = Counter(entries)
  val deqPtr = Counter(entries)
  val maybeFull = RegInit(false.B)

  val ptrMatch = enqPtr.value === deqPtr.value
  val empty = ptrMatch && !maybeFull
  val full  = ptrMatch && maybeFull

  // 丢弃入队的无效指令（inst_valid=false 的指令直接丢弃）
  val dropInvalidEnq = io.enq.valid && !getInstValid(io.enq.bits)

  val doEnq = WireDefault(io.enq.fire && !dropInvalidEnq)
  val doDeq = WireDefault(io.deq.fire)

  // 出队数据
  val deqBitsBase = Wire(chiselTypeOf(io.enq.bits))
  deqBitsBase := ram(deqPtr.value)

  // 基本握手逻辑
  io.deq.valid := !empty
  io.enq.ready := !full || dropInvalidEnq

  // flow 模式：空时直接透传
  val bypassFlow = WireDefault(false.B)
  if (flow) {
    when(io.enq.valid) {
      io.deq.valid := true.B
    }
    when(empty && !dropInvalidEnq) {
      deqBitsBase := io.enq.bits
      bypassFlow := io.enq.valid
      doDeq := false.B
      when(io.deq.ready) {
        doEnq := false.B
      }
    }
    when(empty && dropInvalidEnq) {
      io.deq.valid := false.B
    }
  }

  // pipe 模式：满时若 deq.ready 也允许入队
  if (pipe) {
    when(io.deq.ready) {
      io.enq.ready := true.B
    }
  }

  // 输出数据：检查 invalidMask，可选检查 flush
  val queuedInvalid = invalidMask(deqPtr.value)

  // 关键：flushAffectsOutput 控制 flush 是否影响当前输出
  // - true: flush 时当前输出也被标记无效（用于 if_id_q）
  // - false: flush 只影响内部条目，当前输出保持原值（用于 id_ex_q，避免组合环路）
  //
  // 注意：flow 模式下 flowInvalid 会创建 enq->deq 的组合路径
  // 当 flow=false 时，不能使用 Mux 包含 flowInvalid，否则会形成组合环路
  val outInvalid = if (flow) {
    val flowInvalid = !getInstValid(io.enq.bits)
    (if (flushAffectsOutput) io.flush else false.B) ||
    Mux(bypassFlow, flowInvalid, queuedInvalid)
  } else {
    (if (flushAffectsOutput) io.flush else false.B) || queuedInvalid
  }
  io.deq.bits := withForcedInvalid(deqBitsBase, outInvalid)

  // 入队
  when(doEnq) {
    ram(enqPtr.value) := io.enq.bits
    invalidMask(enqPtr.value) := !getInstValid(io.enq.bits)
    enqPtr.inc()
  }

  // 出队
  when(doDeq) {
    deqPtr.inc()
  }

  // 更新 maybeFull
  when(doEnq =/= doDeq) {
    maybeFull := doEnq
  }

  // flush 语义：不清空队列，将所有元素标记为无效
  when(io.flush) {
    for (i <- 0 until entries) {
      invalidMask(i) := true.B
    }
  }

  // 计数
  val ptrDiff = enqPtr.value - deqPtr.value
  if (isPow2(entries)) {
    io.count := Mux(maybeFull && ptrMatch, entries.U, 0.U) | ptrDiff
  } else {
    io.count := Mux(
      ptrMatch,
      Mux(maybeFull, entries.asUInt, 0.U),
      Mux(deqPtr.value > enqPtr.value, entries.asUInt + ptrDiff, ptrDiff)
    )
  }

  override def desiredName: String = s"PipeReg${entries}_${gen.typeName}"
}

/**
  * PipeReg 伴生对象：提供便捷的工厂方法
  */
object PipeReg {
  /**
    * 创建 PipeReg 并连接到上游 Decoupled 接口
    *
    * @param enq                上游 Decoupled 输出
    * @param flush              冲刷信号（标记队列中所有指令无效）
    * @param entries            队列深度（默认 1）
    * @param pipe               pipe 模式
    * @param flow               flow 模式
    * @param flushAffectsOutput flush 是否影响当前输出（默认 true）
    * @return                   下游 Decoupled 接口
    */
  def apply[T <: Data](
      enq: ReadyValidIO[T],
      flush: Bool,
      entries: Int = 1,
      pipe: Boolean = false,
      flow: Boolean = false,
      flushAffectsOutput: Boolean = true
  ): DecoupledIO[T] = {
    val q = Module(new PipeReg(chiselTypeOf(enq.bits), entries, pipe, flow, flushAffectsOutput))
    q.io.enq.valid := enq.valid
    q.io.enq.bits  := enq.bits
    q.io.flush     := flush
    enq.ready      := q.io.enq.ready
    q.io.deq
  }
}
