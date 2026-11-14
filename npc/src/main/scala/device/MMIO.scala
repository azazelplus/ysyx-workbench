package device

import chisel3._
import _root_.core.CoreConfig
import bus.AXI4SlaveBus
import azazerv.{MMIORead, MMIOWrite}

/** UART / RTC 的 AXI4 从机，接在 Xbar.mmio 后.
  * 把总线上的读写请求按 R事务 和 W事务 分成 MMIORead/MMIOWrite 两个子设备模块的访问.
  * 当前设备接口只接收对齐的 32 位单拍访问，实际字节写入由 WSTRB 决定。
  * AR 握手后读取一次设备并锁存结果；AW / W 可独立到达，配对后写一次设备。
  * pending 保持 R / B 响应，直到上游接收，期间不会重复访问设备。
  */
class MMIO(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    val axi = new AXI4SlaveBus(cfg.bus)
  })

  // 实例化两个子模块
  val mmio_read = Module(new MMIORead)
  val mmio_write = Module(new MMIOWrite)
  mmio_read.clock := clock
  mmio_write.clock := clock

  // pending寄存器. 表示收到R/W事务请求, 响应还没有到来或到来了还没有接收. 用来单事务锁存, 从而不允许多事务在途(outstanding).
  val read_pending = RegInit(false.B) //  read_pending寄存器的作用是T周期ar.fire后, 在T+1周期关闭下一笔AR的入口 + 打开这笔R的出口. 最后一拍R握手后下个周期被清0.
  val aw_pending = RegInit(false.B)   // aw_pending寄存器的作用是T周期aw.fire后, 在T+1周期关闭下一笔AW的入口 + 和w_pending与门打开这笔B的出口. B握手后下个周期被清0.
  val w_pending = RegInit(false.B)    // 类比aw_pending.

  // AXI通道payload
  val read_id = Reg(UInt(cfg.bus.idBits.W))       //AR/R
  val write_addr = Reg(UInt(cfg.addrBits.W))      //AW
  val write_id = Reg(UInt(cfg.bus.idBits.W))      //AW
  val write_data = Reg(UInt(cfg.dataBits.W))      //W
  val write_strb = Reg(UInt(cfg.bus.strbBits.W))  //W
  // 读使能就是ar.fire && !reset



  // 默认输出
  // 读: 
  io.axi.ar.ready := !read_pending
  io.axi.r.valid := read_pending
  io.axi.r.bits := 0.U.asTypeOf(io.axi.r.bits)
  io.axi.r.bits.id := read_id
  io.axi.r.bits.data := mmio_read.rdata
  io.axi.r.bits.last := true.B
  mmio_read.ren := io.axi.ar.fire && !reset.asBool
  mmio_read.raddr := io.axi.ar.bits.addr
  // 写：分别保存先到达的 AW 或 W，最后一个缺少的通道握手时配对。
  io.axi.aw.ready := !aw_pending
  io.axi.w.ready := !w_pending
  io.axi.b.valid := aw_pending && w_pending
  io.axi.b.bits := 0.U.asTypeOf(io.axi.b.bits)
  io.axi.b.bits.id := write_id
  // 写使能: AW/W在另一方已经到达等待时到达, 从而凑齐时.
  mmio_write.wen := ((io.axi.aw.fire && (w_pending || io.axi.w.fire)) || (io.axi.w.fire && aw_pending)) && !reset.asBool
  mmio_write.waddr := Mux(aw_pending, write_addr, io.axi.aw.bits.addr)
  mmio_write.wdata := Mux(w_pending, write_data, io.axi.w.bits.data)
  mmio_write.wmask := Mux(w_pending, write_strb, io.axi.w.bits.strb)

  // 握手行为
  when(io.axi.ar.fire) {
    assert(io.axi.ar.bits.len === 0.U && io.axi.ar.bits.size === 2.U &&
      io.axi.ar.bits.addr(1, 0) === 0.U,
      "MMIO only supports aligned 32-bit single-beat reads")
    read_pending := true.B
    read_id := io.axi.ar.bits.id
  }
  when(io.axi.r.fire) { read_pending := false.B }

  when(io.axi.aw.fire) {
    assert(io.axi.aw.bits.len === 0.U && io.axi.aw.bits.size === 2.U &&
      io.axi.aw.bits.addr(1, 0) === 0.U,
      "MMIO only supports aligned 32-bit single-beat writes")
    aw_pending := true.B
    write_addr := io.axi.aw.bits.addr
    write_id := io.axi.aw.bits.id
  }
  when(io.axi.w.fire) {
    assert(io.axi.w.bits.last, "MMIO expects one W beat")
    w_pending := true.B
    write_data := io.axi.w.bits.data
    write_strb := io.axi.w.bits.strb
  }
  when(io.axi.b.fire) {
    aw_pending := false.B
    w_pending := false.B
  }
}
