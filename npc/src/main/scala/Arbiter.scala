/*
AXI4 2-to-1 仲裁器
将 IFU 和 LSU 两个 Master 的请求仲裁后转发给下游（PMEM 或 Xbar）

拥有3个pending寄存器. 承诺:
1.同时只接收一笔R/W请求. AR/AW握手后关闭对应请求通道, W最后一拍握手后关闭W通道; 直到R最后一拍/B握手事务完成前, 不发出下一笔事务的请求.
2.上游没有ready时保持当前授权, 透传下游保持的R/B响应, 并向下游传递反压.

仲裁策略: 固定优先级 (LSU > IFU)
- LSU 数据访问在关键路径，优先处理
- IFU 取指可容忍短暂延迟（有 Queue 缓冲）

状态机:
  s_idle:      空闲，检查请求，优先授权 LSU
  s_ifu_grant: IFU 获得总线，转发 AR→R 事务
  s_lsu_grant: LSU 获得总线，转发读(AR→R)或写(AW+W→B)事务
*/
package azazerv

import chisel3._
import _root_.core.CoreConfig
import chisel3.util._
import bus.{AXI4MasterBus, AXI4SlaveBus}

class AXI4Arbiter(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    // 按对端角色命名：master0/1 对接上游主机，slave 对接下游从机。
    // 方向以 Arbiter 本端为准：master0/1 是从机端口，slave 是主机端口。
    val master0 = new AXI4SlaveBus(cfg.bus)   // 对端 IFU，只使用 AR / R
    val master1 = new AXI4SlaveBus(cfg.bus)   // 对端 LSU，使用全部通道
    val slave   = new AXI4MasterBus(cfg.bus)  // 对端当前是 PMEM，接入 RAM 时换成 Xbar
  })

  // ========== 状态机 ==========
  val s_idle :: s_ifu_grant :: s_lsu_grant :: Nil = Enum(3)
  val state = RegInit(s_idle)

  // 记录 LSU 当前是读还是写事务
  val lsu_is_write = RegInit(false.B)

  // 当前事务已经握手的请求通道。AR/AW握手后关闭对应通道，W最后一拍握手后关闭，直到响应完成。
  // AW / W 独立记录，允许写地址和写数据以任意先后顺序握手。
  val read_pending = RegInit(false.B)
  val aw_pending = RegInit(false.B)
  val w_pending = RegInit(false.B)
  val write_response_pending = aw_pending && w_pending

  when(io.slave.ar.fire) { read_pending := true.B }
  when(io.slave.r.fire && io.slave.r.bits.last) { read_pending := false.B }
  when(io.slave.aw.fire) { aw_pending := true.B }
  when(io.slave.w.fire && io.slave.w.bits.last) { w_pending := true.B }
  when(io.slave.b.fire) {
    aw_pending := false.B
    w_pending := false.B
  }

  // ========== 默认值: 阻塞所有 Master，Slave 不发请求 ==========

  // IFU 端口默认阻塞
  io.master0.ar.ready := false.B
  io.master0.r.valid  := false.B
  io.master0.r.bits   := 0.U.asTypeOf(io.master0.r.bits)
  io.master0.aw.ready := false.B
  io.master0.w.ready  := false.B
  io.master0.b.valid  := false.B
  io.master0.b.bits   := 0.U.asTypeOf(io.master0.b.bits)

  // LSU 端口默认阻塞
  io.master1.ar.ready := false.B
  io.master1.r.valid  := false.B
  io.master1.r.bits   := 0.U.asTypeOf(io.master1.r.bits)
  io.master1.aw.ready := false.B
  io.master1.w.ready  := false.B
  io.master1.b.valid  := false.B
  io.master1.b.bits   := 0.U.asTypeOf(io.master1.b.bits)

  // 下游端口默认不发请求
  io.slave.ar.valid     := false.B
  io.slave.ar.bits      := 0.U.asTypeOf(io.slave.ar.bits)
  io.slave.r.ready      := false.B
  io.slave.aw.valid     := false.B
  io.slave.aw.bits      := 0.U.asTypeOf(io.slave.aw.bits)
  io.slave.w.valid      := false.B
  io.slave.w.bits       := 0.U.asTypeOf(io.slave.w.bits)
  io.slave.b.ready      := false.B

  // ========== 状态机逻辑 ==========
  switch(state) {
    is(s_idle) {
      // 固定优先级: LSU > IFU
      // 检查 LSU 是否有请求 (读或写)
      when(io.master1.ar.valid || io.master1.aw.valid) {
        lsu_is_write := io.master1.aw.valid  // 记录是写还是读
        state := s_lsu_grant
      }.elsewhen(io.master0.ar.valid) {
        // IFU 只会发读请求
        state := s_ifu_grant
      }
    }

    is(s_ifu_grant) {
      // IFU 获得总线: 转发 AR/R 通道
      // AR: IFU → 下游
      io.slave.ar.valid     := io.master0.ar.valid && !read_pending
      io.slave.ar.bits      := io.master0.ar.bits
      io.master0.ar.ready   := io.slave.ar.ready && !read_pending

      // R: 下游 → IFU
      io.master0.r.valid := io.slave.r.valid && read_pending
      io.master0.r.bits  := io.slave.r.bits
      io.slave.r.ready   := io.master0.r.ready && read_pending

      // 事务完成: R 通道最后一拍握手成功
      when(io.slave.r.fire && io.slave.r.bits.last) {
        state := s_idle
      }
    }

    is(s_lsu_grant) {
      when(lsu_is_write) {
        // LSU 写事务: 转发 AW/W/B 通道
        // AW: LSU → 下游
        io.slave.aw.valid     := io.master1.aw.valid && !aw_pending
        io.slave.aw.bits      := io.master1.aw.bits
        io.master1.aw.ready   := io.slave.aw.ready && !aw_pending

        // W: LSU → 下游
        io.slave.w.valid     := io.master1.w.valid && !w_pending
        io.slave.w.bits      := io.master1.w.bits
        io.master1.w.ready   := io.slave.w.ready && !w_pending

        // B: 下游 → LSU
        io.master1.b.valid := io.slave.b.valid && write_response_pending
        io.master1.b.bits  := io.slave.b.bits
        io.slave.b.ready   := io.master1.b.ready && write_response_pending

        // 事务完成: B 通道握手成功
        when(io.slave.b.fire) {
          state := s_idle
        }
      }.otherwise {
        // LSU 读事务: 转发 AR/R 通道
        // AR: LSU → 下游
        io.slave.ar.valid     := io.master1.ar.valid && !read_pending
        io.slave.ar.bits      := io.master1.ar.bits
        io.master1.ar.ready   := io.slave.ar.ready && !read_pending

        // R: 下游 → LSU
        io.master1.r.valid := io.slave.r.valid && read_pending
        io.master1.r.bits  := io.slave.r.bits
        io.slave.r.ready   := io.master1.r.ready && read_pending

        // 事务完成: R 通道最后一拍握手成功
        when(io.slave.r.fire && io.slave.r.bits.last) {
          state := s_idle
        }
      }
    }
  }
}
