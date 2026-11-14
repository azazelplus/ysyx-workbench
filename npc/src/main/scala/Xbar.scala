package azazerv

import chisel3._
import chisel3.util._
import _root_.core.CoreConfig
import bus.{AXI4ErrorSlave, AXI4MasterBus, AXI4SlaveBus}

/** 地址分发器
  * 接在 Arbiter 后，根据请求地址选择 RAM 或 MMIO 从机。
  * 对上游Arb提供从机端口in, 对下游提供3个主机端口(ram, mmio, error), 其中error是内部连线.
  * 上游 Arbiter 保证最多一笔未完成事务. 本模块只保存路由(target寄存器). 如果上游在 s_read 期间又发一个 AR, 会直接把它转发给同一个 target, 因为 state === s_read 时 port.ar.valid := io.in.ar.valid , 第二个 AR 会混进当前事务...
  *
  * 硬件地址参数默认对应 scripts/platform/npc.mk. 与 PC 复位地址独立
  * 原样转发物理地址. s_idle时直接译码、转发请求，保存目标直到 R最后一拍 / B 握手。
  * 未映射地址交给内部 AXI4ErrorSlave, 按从机协议返回 DECERR。
  */
class Xbar(
    val ramBase: Long = 0x80000000L,
    val ramSize: Long = 0x08000000L,
    val uartBase: Long = 0x10000000L,
    val rtcBase: Long = 0xa0000048L
)(implicit val cfg: CoreConfig) extends Module {
  // 检查配置合法
  private val regions = Seq((ramBase, ramSize), (uartBase, 4L), (rtcBase, 8L))
  for ((base, size) <- regions) {
    require(base >= 0 && size >= 4 && base % 4 == 0 && size % 4 == 0 &&
      BigInt(base) + size <= (BigInt(1) << cfg.addrBits),
      "Xbar regions must be word aligned and fit in the address space")
  }
  for (i <- regions.indices; j <- 0 until i) {
    val (a, aSize) = regions(i)
    val (b, bSize) = regions(j)
    require(a + aSize <= b || b + bSize <= a, "Xbar regions must not overlap")
  }
  //检查 end

  val io = IO(new Bundle {
    // 按连接对象命名：ram/mmio 指对端从机，接口方向以 Xbar 本端为准。
    val in   = new AXI4SlaveBus(cfg.bus)   // 对端 Arbiter 是主机，本端是从机
    val ram  = new AXI4MasterBus(cfg.bus)  // 对端 RAM 是从机，本端是主机
    val mmio = new AXI4MasterBus(cfg.bus)  // 对端 MMIO 设备是从机，本端是主机
  })

  // 判断结果常数约定: 0为ram区间, 1为mmio区间, 2为未定义地址.
  private val targetRam = 0.U(2.W)
  private val targetMmio = 1.U(2.W)
  private val targetError = 2.U(2.W)

  private val errorSlave = Module(new AXI4ErrorSlave(cfg.bus)) //错误地址处理子模块
  private val error = Wire(new AXI4MasterBus(cfg.bus)) // 对端是内部错误从机，本端是主机
  errorSlave.io.axi <> error
  private val ports = Seq(io.ram, io.mmio, error) // 打包面向所有下游的主机端口.

  // 区间判断辅助函数, 供decode调用.
  private def inRange(addr: UInt, base: Long, size: Long): Bool = {
    val wideAddr = Cat(0.U(1.W), addr)  // 多扩一位，以表达 2^32 这个区间右端点（不包含），避免 RV32 地址上界回绕
    wideAddr >= base.U((cfg.addrBits + 1).W) &&
      wideAddr < (base + size).U((cfg.addrBits + 1).W)
  }

  // 判断地址属于哪个区间. 返回0, 1, 2.
  private def decode(addr: UInt): UInt = MuxCase(targetError, Seq(
    inRange(addr, ramBase, ramSize) -> targetRam,
    (inRange(addr, uartBase, 4) || inRange(addr, rtcBase, 8)) -> targetMmio
  ))

  // Xbar内部寄存器.
  val s_idle :: s_read :: s_write :: Nil = Enum(3)
  val state = RegInit(s_idle)
  val target = RegInit(targetError) // target寄存器, 存储选中的下游target. 整个事务期间保持, 直到R最后一拍/B握手

  // 空闲时用当前地址直接选路；之后使用锁存的目标。
  // W 先于 AW 到达时, 等待 AW 提供地址.
  // start_write, start_read本质上是旁路state寄存器的信号.


  
  val start_write = (state === s_idle) && io.in.aw.valid
  val start_read = (state === s_idle) && !io.in.aw.valid && io.in.ar.valid
  val route = Mux(state === s_idle,
    decode(Mux(io.in.aw.valid, io.in.aw.bits.addr, io.in.ar.bits.addr)), target)

  // 默认输出。
  io.in.ar.ready := false.B
  io.in.r.valid := false.B
  io.in.r.bits := 0.U.asTypeOf(io.in.r.bits)
  io.in.aw.ready := false.B
  io.in.w.ready := false.B
  io.in.b.valid := false.B
  io.in.b.bits := 0.U.asTypeOf(io.in.b.bits)
  for (port <- ports) {
    port.ar.valid := false.B
    port.ar.bits := io.in.ar.bits
    port.r.ready := false.B
    port.aw.valid := false.B
    port.aw.bits := io.in.aw.bits
    port.w.valid := false.B
    port.w.bits := io.in.w.bits
    port.b.ready := false.B
  }

  // 目标匹配后透明转发。AR/AW通道只握手一次，W通道传输到最后一拍，由上游 Arbiter 保证。
  // `zipWithIndex`方法: 给集合中每个元素打包成元素+其下标的二元组.
  // 就像{a, b, c}.zipWithIndex得到((a,0),(b,1),(c,2)).
  // 所以ports.zipWithIndex == Seq((io.ram, 0), (io.mmio, 1), (error, 2)).
  // 下面的when块三轮循环里, `port`和`index`依次对应RAM、MMIO和错误从机及其下标.
  for ((port, index) <- ports.zipWithIndex) {
    when(route === index.U) {
      when(start_read || state === s_read) {
        port.ar.valid := io.in.ar.valid
        io.in.ar.ready := port.ar.ready
      }
      when(state === s_read) {
        io.in.r.valid := port.r.valid
        io.in.r.bits := port.r.bits
        port.r.ready := io.in.r.ready
      }
      when(start_write || state === s_write) {
        port.aw.valid := io.in.aw.valid
        io.in.aw.ready := port.aw.ready
        port.w.valid := io.in.w.valid
        io.in.w.ready := port.w.ready
      }
      when(state === s_write) {
        io.in.b.valid := port.b.valid

        io.in.b.bits := port.b.bits
        port.b.ready := io.in.b.ready
      }
    }
  }

  // 首次出现请求时锁定方向和目标；上面的转发电路当拍已经生效。
  switch(state) {
    is(s_idle) {
      when(start_write) {
        target := route
        state := s_write
      }.elsewhen(start_read) {
        target := route
        state := s_read
      }
    }
    is(s_read) {
      when(io.in.r.fire && io.in.r.bits.last) { state := s_idle }
    }
    is(s_write) {
      when(io.in.b.fire) { state := s_idle }
    }
  }
}

/* 
note: 
  如何判断AXI链路里是否发生了延迟呢? 这个Xbar塞在Arb和mem之间是否会引入一拍延迟?
  以读事务为例, 我们需要检查AR, R通道的一次传输, 找出它被放行所需的信息, 判断这些信息是否已经齐备, 还是必须为了本次传输额外等待一次时钟更新.

  注意when块其实也包含了赋值! 
    when(cond){sig := expr} 硬件上等于 sig := Mux{cond, expr, sig}.
  switch块是多个when块.
  switch(state) {
    is(s_idle) { ... }
    is(s_read) { ... }
    is(s_write) { ... }
  }
  等价于
  when(state === s_idle) { ... }
  .elsewhen(state === s_read) { ... }
  .elsewhen(state === s_write) { ... }
我们现在来检查R事务. T时刻, Xbar空闲, 上游arb发来一笔RAM读请求, 其ar.valid=1, 下游ram也空闲, 其ar.ready=1. 
上游ar.valid接到哪里? 搜索发现, `port.ar.valid := io.in.ar.valid`在when块里. 此处的port就是ram. 检查when块:
      when(start_read || state === s_read) {
        port.ar.valid := io.in.ar.valid
        io.in.ar.ready := port.ar.ready
      }  
当`start_read || state === s_read`时, 上游ar.valid透传给ram


 */