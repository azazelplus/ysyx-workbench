// MiniRV 统一物理存储器模块
// 封装所有 DPI-C 存储器访问接口
// 提供统一的 AXI-Lite Slave 接口给 Arbiter

package minirv

import chisel3._
import chisel3.util._

/**
  * PMEM - 统一物理存储器模块
  *
  * 功能：
  * 1. 提供统一的 AXI-Lite Slave 接口
  * 2. 处理来自 Arbiter 的读写请求 (IFU 取指 + LSU 数据访问)
  * 3. EBREAK 指令检测 - 用于仿真终止
  *
  * 所有读操作行为: 1-cycle SRAM (AR.fire 后下一周期 R.valid 拉高)
  * 所有写操作行为: 1-cycle (AW+W.fire 后下一周期 B.valid 拉高)
  *
  * 注意: Arbiter 保证同一时刻只有一个读或写请求，不会同时出现。
  */
class PMEM extends Module {
  val io = IO(new Bundle {
    // ========== 统一的 AXI-Lite Slave 接口 ==========
    val axi = new AXILiteSlaveIO

    // ========== EBREAK 检测接口 MiniRV->PMEM ==========
    val ebreak_inst  = Input(UInt(32.W))
    val ebreak_valid = Input(Bool())
  })

  // ========== 内部实例化 DPI-C 模块 ==========
  val pmem_read  = Module(new PMEMRead)
  val pmem_write = Module(new PMEMWrite)

  pmem_read.clock  := clock
  pmem_write.clock := clock

  // =====================================================================
  // 读端状态机 (AR/R): 1-cycle SRAM
  // =====================================================================
  val rd_idle :: rd_resp :: Nil = Enum(2)
  val rdState = RegInit(rd_idle)
  val rdAddrReg = Reg(UInt(Config.ADDR_WIDTH.W))

  pmem_read.raddr := rdAddrReg

  // AR/R 通道默认值
  io.axi.ar.ready     := false.B
  io.axi.r.valid      := false.B
  io.axi.r.bits.data  := pmem_read.rdata
  io.axi.r.bits.resp  := 0.U  // OK


  switch(rdState) {
    is(rd_idle) {
      io.axi.ar.ready := true.B
      when(io.axi.ar.fire) {
        rdAddrReg := io.axi.ar.bits.addr
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
  val wrAddrReg = Reg(UInt(Config.ADDR_WIDTH.W))
  val wrDataReg = Reg(UInt(Config.XLEN.W))
  val wrStrbReg = Reg(UInt(4.W))

  // PMEMWrite 默认不写
  pmem_write.wen   := false.B
  pmem_write.waddr := wrAddrReg
  pmem_write.wdata := wrDataReg
  pmem_write.wmask := wrStrbReg

  // AW/W/B 通道默认值
  io.axi.aw.ready   := false.B
  io.axi.w.ready    := false.B
  io.axi.b.valid    := false.B
  io.axi.b.bits.resp := 0.U  // OK

  switch(wrState) {
    is(wr_idle) {
      // 同时接受 AW 和 W (要求 LSU 同时拉高两者)
      io.axi.aw.ready := io.axi.w.valid   // 只有 W 也 valid 时才 ready
      io.axi.w.ready  := io.axi.aw.valid  // 只有 AW 也 valid 时才 ready

      when(io.axi.aw.fire && io.axi.w.fire) {
        wrAddrReg := io.axi.aw.bits.addr
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

  // =====================================================================
  // EBREAK 检测模块
  // =====================================================================
  val ebreak_detect = Module(new EBREAKDetect)
  ebreak_detect.clock := clock
  ebreak_detect.inst  := io.ebreak_inst
  ebreak_detect.valid := io.ebreak_valid
}
