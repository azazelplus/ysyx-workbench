package bus

import chisel3._

/** 未映射地址的 AXI4 从机.
 * 读返回零和 DECERR; 按ARLEN+1返回各拍, 最后一拍置RLAST, 并返回ARID.
 * 写接收 AW 和最后一拍 W 后返回 DECERR, BID对应AWID.
 *
 * 拥有一组pending寄存器, 同Arb的设计.
1.读方向和写方向各只接收一笔未完成事务. AR/AW握手后, 直到对应R最后一拍/B握手事务完成前, 关闭该方向下一笔地址请求的入口; W在最后一拍握手后关闭入口.
2.上游没有ready时保持错误响应.
read_pending: T周期ar.fire后, T+1周期闭下一笔AR的入口 + 打开这笔R各拍的出口. 最后一拍R握手后下个周期被清0.
aw_pending: T周期aw.fire后, T+1周期闭下一笔AW的入口; 和w_pending共同决定这笔B的出口. B握手后下个周期和w_pending一起被清0.
w_pending: T周期最后一拍w.fire后, T+1周期闭下一笔W的入口; 和aw_pending共同决定这笔B的出口.
承诺: R/B返回对应请求的ID, 响应有效后保持到上游握手.
 */
class AXI4ErrorSlave(val cfg: BusConfig) extends Module {
  val io = IO(new Bundle {
    val axi = new AXI4SlaveBus(cfg)
  })

  private val decerr = 3.U(2.W) // AXI的R/B通道的resp(2bit)字段规定`3`为地址解码错误.

  // 记录已经接收的AR/AW/W请求, 上游没有ready时保持错误响应.
  // * 当前IFU/LSU在R/W响应有效时总是已r/w.ready.
  val read_pending = RegInit(false.B) // read_pending寄存器的作用是T周期ar.fire后, 在T+1周期关闭下一笔AR的入口 + 打开这笔R的出口. 最后一拍R握手后下个周期被清0.
  val read_id = Reg(UInt(cfg.idBits.W))
  val read_beats_left = Reg(UInt(9.W)) // 1..256，能够表示 ARLEN=255 的突发。

  // 写地址与写数据可以独立到达。
  val aw_pending = RegInit(false.B) // aw_pending寄存器的作用是T周期aw.fire后, 在T+1周期关闭下一笔AW的入口 + 和w_pending与门打开这笔B的出口. B握手后下个周期被清0.
  val w_pending = RegInit(false.B) // T周期最后一拍w.fire后置位, T+1周期关闭下一笔W的入口 + 和aw_pending与门打开这笔B的出口.
  val write_id = Reg(UInt(cfg.idBits.W))

  // 默认输出
  io.axi.ar.ready := !read_pending // read_pending默认为0, 所以默认ar.ready等待握手. 若在T拍上游ar.valid, 则T拍ar.fire. T+1拍时, pending寄存器置位, 从而ar.ready=0拒绝ar握手.
  io.axi.r.valid := read_pending
  io.axi.r.bits := 0.U.asTypeOf(io.axi.r.bits)
  io.axi.r.bits.id := read_id
  io.axi.r.bits.resp := decerr
  io.axi.r.bits.last := read_beats_left === 1.U
  io.axi.aw.ready := !aw_pending
  io.axi.w.ready := !w_pending
  io.axi.b.valid := aw_pending && w_pending
  io.axi.b.bits := 0.U.asTypeOf(io.axi.b.bits)
  io.axi.b.bits.id := write_id
  io.axi.b.bits.resp := decerr

  // 握手时,
  when(io.axi.ar.fire) {
    read_pending := true.B
    read_id := io.axi.ar.bits.id
    read_beats_left := io.axi.ar.bits.len +& 1.U
  }
  when(io.axi.r.fire) {
    read_beats_left := read_beats_left - 1.U
    when(io.axi.r.bits.last) { read_pending := false.B }
  }
  when(io.axi.aw.fire) {
    aw_pending := true.B
    write_id := io.axi.aw.bits.id
  }
  when(io.axi.w.fire && io.axi.w.bits.last) { w_pending := true.B }
  when(io.axi.b.fire) {
    aw_pending := false.B
    w_pending := false.B
  }
}
