// AXI-Lite 2-to-1 仲裁器
// 将 IFU 和 LSU 两个 Master 的请求仲裁后转发给 PMEM Slave
//
// 仲裁策略: 固定优先级 (LSU > IFU)
// - LSU 数据访问在关键路径，优先处理
// - IFU 取指可容忍短暂延迟（有 Queue 缓冲）
//
// 状态机:
//   s_idle:      空闲，检查请求，优先授权 LSU
//   s_ifu_grant: IFU 获得总线，转发 AR→R 事务
//   s_lsu_grant: LSU 获得总线，转发读(AR→R)或写(AW+W→B)事务

package minirv

import chisel3._
import chisel3.util._

class AXILiteArbiter extends Module {
  val io = IO(new Bundle {
    // 两个 Master 的AXI输入 (从 Arbiter 视角是 Flipped)
    val master0 = Flipped(new AXILiteMasterIO)  // IFU (只用 AR/R)
    val master1 = Flipped(new AXILiteMasterIO)  // LSU (用全部通道)
    // 一个 Slave 输出
    val slave   = new AXILiteMasterIO           // to PMEM
  })

  // ========== 状态机 ==========
  val s_idle :: s_ifu_grant :: s_lsu_grant :: Nil = Enum(3)
  val state = RegInit(s_idle)

  // 记录 LSU 当前是读还是写事务
  val lsu_is_write = RegInit(false.B)

  // ========== 默认值: 阻塞所有 Master，Slave 不发请求 ==========

  // Master0 (IFU) 默认阻塞
  io.master0.ar.ready := false.B
  io.master0.r.valid  := false.B
  io.master0.r.bits   := 0.U.asTypeOf(new AXILiteR)
  io.master0.aw.ready := false.B
  io.master0.w.ready  := false.B
  io.master0.b.valid  := false.B
  io.master0.b.bits   := 0.U.asTypeOf(new AXILiteB)

  // Master1 (LSU) 默认阻塞
  io.master1.ar.ready := false.B
  io.master1.r.valid  := false.B
  io.master1.r.bits   := 0.U.asTypeOf(new AXILiteR)
  io.master1.aw.ready := false.B
  io.master1.w.ready  := false.B
  io.master1.b.valid  := false.B
  io.master1.b.bits   := 0.U.asTypeOf(new AXILiteB)

  // Slave 默认不发请求
  io.slave.ar.valid     := false.B
  io.slave.ar.bits.addr := 0.U
  io.slave.r.ready      := false.B
  io.slave.aw.valid     := false.B
  io.slave.aw.bits.addr := 0.U
  io.slave.w.valid      := false.B
  io.slave.w.bits.data  := 0.U
  io.slave.w.bits.strb  := 0.U
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
      // AR: Master0 → Slave
      io.slave.ar.valid     := io.master0.ar.valid
      io.slave.ar.bits.addr := io.master0.ar.bits.addr
      io.master0.ar.ready   := io.slave.ar.ready

      // R: Slave → Master0
      io.master0.r.valid := io.slave.r.valid
      io.master0.r.bits  := io.slave.r.bits
      io.slave.r.ready   := io.master0.r.ready

      // 事务完成: R 通道握手成功
      when(io.slave.r.fire) {
        state := s_idle
      }
    }

    is(s_lsu_grant) {
      when(lsu_is_write) {
        // LSU 写事务: 转发 AW/W/B 通道
        // AW: Master1 → Slave
        io.slave.aw.valid     := io.master1.aw.valid
        io.slave.aw.bits.addr := io.master1.aw.bits.addr
        io.master1.aw.ready   := io.slave.aw.ready

        // W: Master1 → Slave
        io.slave.w.valid     := io.master1.w.valid
        io.slave.w.bits.data := io.master1.w.bits.data
        io.slave.w.bits.strb := io.master1.w.bits.strb
        io.master1.w.ready   := io.slave.w.ready

        // B: Slave → Master1
        io.master1.b.valid := io.slave.b.valid
        io.master1.b.bits  := io.slave.b.bits
        io.slave.b.ready   := io.master1.b.ready

        // 事务完成: B 通道握手成功
        when(io.slave.b.fire) {
          state := s_idle
        }
      }.otherwise {
        // LSU 读事务: 转发 AR/R 通道
        // AR: Master1 → Slave
        io.slave.ar.valid     := io.master1.ar.valid
        io.slave.ar.bits.addr := io.master1.ar.bits.addr
        io.master1.ar.ready   := io.slave.ar.ready

        // R: Slave → Master1
        io.master1.r.valid := io.slave.r.valid
        io.master1.r.bits  := io.slave.r.bits
        io.slave.r.ready   := io.master1.r.ready

        // 事务完成: R 通道握手成功
        when(io.slave.r.fire) {
          state := s_idle
        }
      }
    }
  }
}
