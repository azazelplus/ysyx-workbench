// AzazeRV 物理存储器模块（当前普通内存的 DPI-C 后端）
// 封装所有 DPI-C 存储器访问接口
// 提供统一的 AXI4 Slave 接口给 Xbar.ram

package azazerv

import chisel3._
import _root_.core.CoreConfig
import chisel3.util._
import bus.AXI4SlaveBus

/**
  * PMEM - 普通内存的物理存储器模块
  *
  * 功能：
  * 1. 提供统一的 AXI4 Slave 接口
  * 2. 处理来自 Xbar.ram 的读写请求 (IFU 取指 + LSU 数据访问)
  * EBREAK 指令检测已移到顶层；UART / RTC 由独立的 MMIO 从机处理。
  *
  * 所有读操作行为: 1-cycle SRAM (AR.fire 后下一周期 R.valid 拉高)
  * 所有写操作行为: 1-cycle (AW+W.fire 后下一周期 B.valid 拉高)
  *
  * Arbiter 保证同一时刻只有一个读或写请求，不会同时出现.
  * 当前 DPI-C 过渡后端只处理对齐的 32 位单拍访问，返回请求 ID 和 RLAST。
  * RAM 的突发读写由 RAM.scala 实现。
  */
class PMEM(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    // ========== 统一的 AXI4 Slave 接口 ==========
    val axi = new AXI4SlaveBus(cfg.bus)

  })

  // ========== 内部实例化 DPI-C 模块 ==========
  val pmem_read  = Module(new PMEMRead)
  val pmem_write = Module(new PMEMWrite)

  pmem_read.clock  := clock
  pmem_write.clock := clock

  // =====================================================================
  // 读端状态机 (AR/R): 1-cycle SRAM. AR握手后, 锁存AR请求, 下一周期输出r.valid并准备好pmem_read.rdata. R握手后下个周期回到空闲.
  // =====================================================================
  val rd_idle :: rd_resp :: Nil = Enum(2)
  val rdState = RegInit(rd_idle)
  //锁存AR请求
  val rdAddrReg = Reg(UInt(cfg.addrBits.W))
  val rdIdReg = Reg(UInt(cfg.bus.idBits.W))

  pmem_read.raddr := rdAddrReg

  // AR/R 通道默认值
  io.axi.ar.ready     := false.B
  io.axi.r.valid      := false.B
  io.axi.r.bits       := 0.U.asTypeOf(io.axi.r.bits)
  io.axi.r.bits.id    := rdIdReg
  io.axi.r.bits.data  := pmem_read.rdata
  io.axi.r.bits.resp  := 0.U  // OK
  io.axi.r.bits.last  := true.B

  // 读状态机
  switch(rdState) {
    is(rd_idle) {
      io.axi.ar.ready := true.B
      when(io.axi.ar.fire) {
        assert(io.axi.ar.bits.len === 0.U && io.axi.ar.bits.size === 2.U &&
          io.axi.ar.bits.addr(1, 0) === 0.U,
          "DPI PMEM only supports aligned 32-bit single-beat reads")
        // 锁存AR请求
        rdAddrReg := io.axi.ar.bits.addr
        rdIdReg := io.axi.ar.bits.id
        // 进入读响应状态
        rdState := rd_resp
      }
    }
    is(rd_resp) {
      io.axi.r.valid := true.B
      io.axi.r.bits.data := pmem_read.rdata
      when(io.axi.r.fire) {
        rdState := rd_idle
      }
    }
  }

  // =====================================================================
  // 写端状态机 (AW/W/B): 1-cycle
  // AW 和 W 同时接受, 下一周期发出 B 响应
  // =====================================================================
  val wr_idle :: wr_resp :: Nil = Enum(2)
  val wrState = RegInit(wr_idle)

  // 锁存写请求
  val wrAddrReg = Reg(UInt(cfg.addrBits.W))
  val wrDataReg = Reg(UInt(cfg.dataBits.W))
  val wrStrbReg = Reg(UInt(4.W))
  val wrIdReg = Reg(UInt(cfg.bus.idBits.W))

  // PMEMWrite 默认不写
  pmem_write.wen   := false.B
  pmem_write.waddr := wrAddrReg
  pmem_write.wdata := wrDataReg
  pmem_write.wmask := wrStrbReg

  // AW/W/B 通道默认值
  io.axi.aw.ready   := false.B
  io.axi.w.ready    := false.B
  io.axi.b.valid    := false.B
  io.axi.b.bits     := 0.U.asTypeOf(io.axi.b.bits)
  io.axi.b.bits.id  := wrIdReg
  io.axi.b.bits.resp := 0.U  // OK

  switch(wrState) {
    is(wr_idle) {
      // 同时接受 AW 和 W (要求 LSU 同时拉高两者)
      io.axi.aw.ready := io.axi.w.valid   // 只有 W 也 valid 时才 ready
      io.axi.w.ready  := io.axi.aw.valid  // 只有 AW 也 valid 时才 ready

      when(io.axi.aw.fire && io.axi.w.fire) {
        assert(io.axi.aw.bits.len === 0.U && io.axi.aw.bits.size === 2.U &&
          io.axi.aw.bits.addr(1, 0) === 0.U && io.axi.w.bits.last,
          "DPI PMEM only supports aligned 32-bit single-beat writes")
        wrAddrReg := io.axi.aw.bits.addr
        wrIdReg := io.axi.aw.bits.id
        wrDataReg := io.axi.w.bits.data
        wrStrbReg := io.axi.w.bits.strb
        // 执行实际写入
        pmem_write.wen   := true.B
        pmem_write.waddr := io.axi.aw.bits.addr
        pmem_write.wdata := io.axi.w.bits.data
        pmem_write.wmask := io.axi.w.bits.strb
        wrState := wr_resp
      }
    }
    is(wr_resp) {
      io.axi.b.valid := true.B
      io.axi.b.bits.resp := 0.U  // OK
      when(io.axi.b.fire) {
        wrState := wr_idle
      }
    }
  }

}
