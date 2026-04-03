





// 供参考学习的实现. 这些模块代码没有被使用.

// 日后我们将要把我们的AXI按照这个学习补充burst, 把仲裁器也学习这个.


package bus

import chisel3._
import chisel3.util._
import os.size

import chisel3.experimental.requireIsChiselType // 这个 import 是为了使用 requireIsChiselType 来检查 gen 的类型

class BusArbiter(
    val masterCount: Int,
    val slaveCount: Int,
    val slaveArea: Seq[Seq[(Long, Long)]]
) extends Module {
  val io = IO(new Bundle {
    val masters = Vec(masterCount, new AXI4SlaveBus)
    val slaves = Vec(slaveCount, new AXI4MasterBus)
  })
  require(
    slaveArea.length == slaveCount,
    s"slaveArea length (${slaveArea.length}) must equal slaveCount ($slaveCount)"
  )

  // master-slave 连接矩阵
  val msMatrix = Vec(masterCount, Vec(slaveCount, Bool()));
  val rReqMat = Wire(msMatrix)
  val rConMat = Wire(msMatrix)
  val rConMatReg = RegInit(0.U.asTypeOf(msMatrix))
  val wReqMat = Wire(msMatrix)
  val wConMat = Wire(msMatrix)
  val wConMatReg = RegInit(0.U.asTypeOf(msMatrix))

  // 读请求矩阵
  for (i <- 0 until masterCount) {
    for (j <- 0 until slaveCount) {
      val addrRange = slaveArea(j)
      val addr = io.masters(i).ar.bits.addr
      val inRange = addrRange
        .map { case (start, end) => addr >= start.U && addr < end.U }
        .reduce(_ || _)
      if (i == 0) {
        rReqMat(i)(j) := inRange && io.masters(i).ar.valid
      } else {
        // 检查是否有更高优先级的 master 已经请求了同一个 slave
        val higherPriorityReq =
          (0 until i).map(k => rReqMat(k)(j)).reduce(_ || _)
        rReqMat(i)(j) := inRange && io.masters(i).ar.valid && !higherPriorityReq
      }
    }
  }
  // 读连接仲裁，slave 优先选择已连接的 master
  for (j <- 0 until slaveCount) {
    val slaveConnected = rConMatReg.map(_(j)).reduce(_ || _)
    for (i <- 0 until masterCount) {
      rConMat(i)(j) := Mux(slaveConnected, rConMatReg(i)(j), rReqMat(i)(j))
    }
  }
  // 更新读连接寄存器
  for (i <- 0 until masterCount) {
    for (j <- 0 until slaveCount) {
      val shakeHand =
        rConMat(i)(j) && io.masters(i).r.valid && io.slaves(j).r.ready
      rConMatReg(i)(j) := (!shakeHand) && rConMat(i)(j)
    }
  }

  // 写请求矩阵
  for (i <- 0 until masterCount) {
    for (j <- 0 until slaveCount) {
      val addrRange = slaveArea(j)
      val addr = io.masters(i).aw.bits.addr
      val inRange = addrRange
        .map { case (start, end) => addr >= start.U && addr < end.U }
        .reduce(_ || _)
      if (i == 0) {
        wReqMat(i)(j) := inRange && io.masters(i).aw.valid
      } else {
        // 检查是否有更高优先级的 master 已经请求了同一个 slave
        val higherPriorityReq =
          (0 until i).map(k => wReqMat(k)(j)).reduce(_ || _)
        wReqMat(i)(j) := inRange && io.masters(i).aw.valid && !higherPriorityReq
      }
    }
  }
  // 更新写连接寄存器
  for (i <- 0 until masterCount) {
    for (j <- 0 until slaveCount) {
      val shakeHand =
        wConMat(i)(j) && io.masters(i).b.valid && io.slaves(j).b.ready
      wConMatReg(i)(j) := Mux(shakeHand, false.B, wConMat(i)(j))
    }
  }
  // 写连接仲裁，slave 优先选择已连接的 master
  for (j <- 0 until slaveCount) {
    val slaveConnected = wConMatReg.map(_(j)).reduce(_ || _)
    for (i <- 0 until masterCount) {
      wConMat(i)(j) := Mux(slaveConnected, wConMatReg(i)(j), wReqMat(i)(j))
    }
  }

  // 连接 master 和 slave
  // slave → master
  for (i <- 0 until masterCount) {
    io.masters(i).aw.ready := io.slaves.zipWithIndex
      .map { case (s, j) => s.aw.ready && wConMat(i)(j) }
      .reduce(_ | _)

    io.masters(i).w.ready := io.slaves.zipWithIndex
      .map { case (s, j) => s.w.ready && wConMat(i)(j) }
      .reduce(_ | _)

    io.masters(i).b.valid := io.slaves.zipWithIndex
      .map { case (s, j) => s.b.valid && wConMat(i)(j) }
      .reduce(_ | _)
    io.masters(i).b.bits := io.slaves.zipWithIndex
      .map { case (s, j) =>
        Fill(s.b.bits.asUInt.getWidth, wConMat(i)(j)) & s.b.bits.asUInt
      }
      .reduce(_ | _)
      .asTypeOf(new AXI4B)

    io.masters(i).ar.ready := io.slaves.zipWithIndex
      .map { case (s, j) => s.ar.ready && rConMat(i)(j) }
      .reduce(_ | _)

    io.masters(i).r.valid := io.slaves.zipWithIndex
      .map { case (s, j) => s.r.valid && rConMat(i)(j) }
      .reduce(_ | _)
    io.masters(i).r.bits := io.slaves.zipWithIndex
      .map { case (s, j) =>
        Fill(s.r.bits.asUInt.getWidth, rConMat(i)(j)) & s.r.bits.asUInt
      }
      .reduce(_ | _)
      .asTypeOf(new AXI4R)
  }
  // master → slave
  for (j <- 0 until slaveCount) {
    io.slaves(j).aw.valid := io.masters.zipWithIndex
      .map { case (m, i) => m.aw.valid && wConMat(i)(j) }
      .reduce(_ | _)
    io.slaves(j).aw.bits := io.masters.zipWithIndex
      .map { case (m, i) =>
        Fill(m.aw.bits.asUInt.getWidth, wConMat(i)(j)) & m.aw.bits.asUInt
      }
      .reduce(_ | _)
      .asTypeOf(new AXI4AW)

    io.slaves(j).w.valid := io.masters.zipWithIndex
      .map { case (m, i) => m.w.valid && wConMat(i)(j) }
      .reduce(_ | _)
    io.slaves(j).w.bits := io.masters.zipWithIndex
      .map { case (m, i) =>
        Fill(m.w.bits.asUInt.getWidth, wConMat(i)(j)) & m.w.bits.asUInt
      }
      .reduce(_ | _)
      .asTypeOf(new AXI4W)

    io.slaves(j).b.ready := io.masters.zipWithIndex
      .map { case (m, i) => m.b.ready && wConMat(i)(j) }
      .reduce(_ | _)

    io.slaves(j).ar.valid := io.masters.zipWithIndex
      .map { case (m, i) => m.ar.valid && rConMat(i)(j) }
      .reduce(_ | _)
    io.slaves(j).ar.bits := io.masters.zipWithIndex
      .map { case (m, i) =>
        Fill(m.ar.bits.asUInt.getWidth, rConMat(i)(j)) & m.ar.bits.asUInt
      }
      .reduce(_ | _)
      .asTypeOf(new AXI4AR)

    io.slaves(j).r.ready := io.masters.zipWithIndex
      .map { case (m, i) => m.r.ready && rConMat(i)(j) }
      .reduce(_ | _)
  }
}



// 优秀的仲裁器实现
// 发送AXI4请求的接口定义
class AXI4MasterBus extends Bundle {
  val ar = Decoupled(new AXI4AR)
  val r = Flipped(Decoupled(new AXI4R))
  val aw = Decoupled(new AXI4AW)
  val w = Decoupled(new AXI4W)
  val b = Flipped(Decoupled(new AXI4B))

  def init(): Unit = {
    ar.valid := false.B
    r.ready := false.B
    aw.valid := false.B
    w.valid := false.B
    b.ready := false.B
    ar.bits := DontCare
    aw.bits := DontCare
    w.bits := DontCare
  }
}

// 接收AXI4请求的接口定义
class AXI4SlaveBus extends Bundle {
  val ar = Flipped(Decoupled(new AXI4AR))
  val r = Decoupled(new AXI4R)
  val aw = Flipped(Decoupled(new AXI4AW))
  val w = Flipped(Decoupled(new AXI4W))
  val b = Decoupled(new AXI4B)

  def init(): Unit = {
    ar.ready := false.B
    r.valid := false.B
    aw.ready := false.B
    w.ready := false.B
    b.valid := false.B
    b.bits := DontCare
    r.bits := DontCare
  }
}

class AXI4AR extends Bundle {
  val addr = UInt(32.W)
  val len = UInt(8.W)
  val size = UInt(3.W)
  val burst = UInt(2.W)
}

class AXI4R extends Bundle {
  val data = UInt(32.W)
  val resp = UInt(2.W)
  val last = Bool()
}

class AXI4AW extends Bundle {
  val addr = UInt(32.W)
  val len = UInt(8.W)
  val size = UInt(3.W)
  val burst = UInt(2.W)
}

class AXI4W extends Bundle {
  val data = UInt(32.W)
  val strb = UInt(4.W)
  val last = Bool()
}

class AXI4B extends Bundle {
  val resp = UInt(2.W)
}


 












/** PipReg 的 IO，接口风格与 QueueIO 接近。
  *
  * 与普通 Queue 的差异在于：
  *   - `flush` 拉高时不清空队列；
  *   - 将队列中元素标记为 `kill = true`；
  *   - 同周期输出也会被强制标记 `kill = true`。
  */
class PipRegIO[T <: Data](private val gen: T, val entries: Int) extends Bundle {
  val enq = Flipped(EnqIO(gen))
  val deq = Flipped(DeqIO(gen))
  val count = Output(UInt(log2Ceil(entries + 1).W))
  val flush = Input(Bool())
}

/** 类似 Queue 的流水寄存器队列，但支持 flush->kill 语义。
  *
  * 约束：`gen` 必须包含 `kill: Bool` 字段。
  */
class PipReg[T <: Data](
    val gen: T,
    val entries: Int,
    val pipe: Boolean = false,
    val flow: Boolean = false,
    val useSyncReadMem: Boolean = false
) extends Module {
  require(entries > 0, "PipReg entries must be greater than 0")
  requireIsChiselType(gen)

  private val genRecord = gen match {
    case r: Record => r
    case _         =>
      throw new IllegalArgumentException(
        "PipReg requires Bundle/Record type containing a Bool field named 'kill'"
      )
  }
  require(
    genRecord.elements.contains("kill"),
    "PipReg payload must contain field 'kill'"
  )

  val io = IO(new PipRegIO(gen, entries))

  private def getKill(x: T): Bool = {
    x.asInstanceOf[Record].elements("kill").asInstanceOf[Bool]
  }

  private def withForcedKill(x: T, forceKill: Bool): T = {
    val out = Wire(chiselTypeOf(x))
    out := x
    when(forceKill) {
      out.asInstanceOf[Record].elements("kill").asInstanceOf[Bool] := true.B
    }
    out
  }

  val ram = if (useSyncReadMem) {
    SyncReadMem(entries, gen, SyncReadMem.WriteFirst)
  } else {
    Mem(entries, gen)
  }

  // 每个队列槽位额外维护一个 kill 标记位：flush 时整体置位。
  val killMask = RegInit(VecInit(Seq.fill(entries)(false.B)))

  val enqPtr = Counter(entries)
  val deqPtr = Counter(entries)
  val maybeFull = RegInit(false.B)

  val ptrMatch = enqPtr.value === deqPtr.value
  val empty = ptrMatch && !maybeFull
  val full = ptrMatch && maybeFull
  val dropKilledEnq = io.enq.valid && getKill(io.enq.bits)

  val doEnq = WireDefault(io.enq.fire && !dropKilledEnq)
  val doDeq = WireDefault(io.deq.fire)

  // 先给 deq 默认值，再在 flow 分支里覆盖。
  val deqBitsBase = Wire(chiselTypeOf(io.enq.bits))
  val deqIdxForMask = WireDefault(deqPtr.value)

  if (useSyncReadMem) {
    val deqPtrNext =
      Mux(deqPtr.value === (entries.U - 1.U), 0.U, deqPtr.value + 1.U)
    deqIdxForMask := Mux(doDeq, deqPtrNext, deqPtr.value)
    deqBitsBase := ram.read(deqIdxForMask)
  } else {
    deqBitsBase := ram(deqPtr.value)
  }

  io.deq.valid := !empty
  io.enq.ready := !full || dropKilledEnq

  val bypassFlow = WireDefault(false.B)
  if (flow) {
    when(io.enq.valid) {
      io.deq.valid := true.B
    }
    when(empty && !dropKilledEnq) {
      deqBitsBase := io.enq.bits
      bypassFlow := io.enq.valid
      doDeq := false.B
      when(io.deq.ready) {
        doEnq := false.B
      }
    }
    when(empty && dropKilledEnq) {
      io.deq.valid := false.B
    }
  }

  if (pipe) {
    when(io.deq.ready) {
      io.enq.ready := true.B
    }
  }

  val queuedKill = killMask(deqIdxForMask)
  val flowKill = getKill(io.enq.bits)
  val outKill = io.flush || Mux(bypassFlow, flowKill, queuedKill)
  io.deq.bits := withForcedKill(deqBitsBase, outKill)

  // 入队/出队指针与容量维护（语义与 Queue 一致）
  when(doEnq) {
    ram(enqPtr.value) := io.enq.bits
    killMask(enqPtr.value) := getKill(io.enq.bits)
    enqPtr.inc()
  }
  when(doDeq) {
    deqPtr.inc()
  }
  when(doEnq =/= doDeq) {
    maybeFull := doEnq
  }

  // flush 语义：不清空，改为将队列中的元素统一标记 kill。
  // 同周期输出 kill 由 outKill 的 io.flush 覆盖保证。
  when(io.flush) {
    for (i <- 0 until entries) {
      killMask(i) := true.B
    }
  }

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

  override def desiredName: String = s"PipReg${entries}_${gen.typeName}"
}

object PipReg {
  def apply[T <: Data](
      enq: ReadyValidIO[T],
      flush: Bool,
      entries: Int = 2,
      pipe: Boolean = false,
      flow: Boolean = false,
      useSyncReadMem: Boolean = false
  ): DecoupledIO[T] = {
    val q = Module(
      new PipReg(chiselTypeOf(enq.bits), entries, pipe, flow, useSyncReadMem)
    )
    q.io.enq.valid := enq.valid
    q.io.enq.bits := enq.bits
    q.io.flush := flush
    enq.ready := q.io.enq.ready
    q.io.deq
  }
}