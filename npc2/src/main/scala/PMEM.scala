// MiniRV 统一物理存储器模块
// 封装所有 DPI-C 存储器访问接口
// IMEM: AXI AR/R (已有)
// DMEM: AXI AR/R + AW/W/B (新增, 1-cycle SRAM 行为)

package minirv

import chisel3._
import chisel3.util._

/**
  * PMEM - 统一物理存储器模块
  *
  * 功能：
  * 1. 提供取指接口 (IMEM) - 给 IFU 使用 (AXI AR/R)
  * 2. 提供数据读写接口 (DMEM) - 给 LSU 使用 (AXI AR/R + AW/W/B)
  * 3. EBREAK 指令检测 - 用于仿真终止
  *
  * 所有读操作行为: 1-cycle SRAM (AR.fire 后下一周期 R.valid 拉高)
  * 所有写操作行为: 1-cycle (AW+W.fire 后下一周期 B.valid 拉高)
  */
class PMEM extends Module {
  val io = IO(new Bundle {
    // ========== 取指接口 (IMEM) IFU<->PMEM ==========
    val imem = Flipped(new IMemIO)

    // ========== 数据存储器接口 (DMEM) LSU<->PMEM ==========
    val dmem = Flipped(new DMemIO)

    // ========== EBREAK 检测接口 MiniRV->PMEM ==========
    val ebreak_inst  = Input(UInt(32.W))
    val ebreak_valid = Input(Bool())
  })

  // ========== 内部实例化 DPI-C 模块 ==========

  // =====================================================================
  // IMEM 端 (取指): AR/R 状态机 (保持不变)
  // =====================================================================
  val imem_read = Module(new PMEMRead)
  imem_read.clock := clock

  val s_idle :: s_wait :: Nil = Enum(2)
  val imemState = RegInit(s_idle)
  val imemAddrReg = Reg(UInt(Config.ADDR_WIDTH.W))

  imem_read.raddr := imemAddrReg

  io.imem.ar.ready := false.B
  io.imem.r.valid := false.B
  io.imem.r.bits.data := imem_read.rdata

  switch(imemState) {
    is(s_idle) {
      io.imem.ar.ready := true.B
      when(io.imem.ar.fire) {
        imemAddrReg := io.imem.ar.bits.addr
        imemState := s_wait
      }
    }

    is(s_wait) {
      io.imem.r.valid := true.B
      io.imem.r.bits.data := imem_read.rdata
      when(io.imem.r.fire) {
        imemState := s_idle
      }
    }
  }

  // =====================================================================
  // DMEM 读端 (Load): AR/R 状态机 (1-cycle SRAM)
  // =====================================================================
  val dmem_read = Module(new PMEMRead)
  dmem_read.clock := clock

  val dm_rd_idle :: dm_rd_resp :: Nil = Enum(2)
  val dmemRdState = RegInit(dm_rd_idle)
  val dmemRdAddrReg = Reg(UInt(Config.ADDR_WIDTH.W))

  dmem_read.raddr := dmemRdAddrReg

  io.dmem.ar.ready := false.B
  io.dmem.r.valid  := false.B
  io.dmem.r.bits.data := dmem_read.rdata

  switch(dmemRdState) {
    is(dm_rd_idle) {
      io.dmem.ar.ready := true.B
      when(io.dmem.ar.fire) {
        dmemRdAddrReg := io.dmem.ar.bits.addr
        dmemRdState := dm_rd_resp
      }
    }
    is(dm_rd_resp) {
      io.dmem.r.valid := true.B
      io.dmem.r.bits.data := dmem_read.rdata
      when(io.dmem.r.fire) {
        dmemRdState := dm_rd_idle
      }
    }
  }

  // =====================================================================
  // DMEM 写端 (Store): AW/W/B 状态机 (1-cycle)
  // AW 和 W 同时接受, 下一周期发出 B 响应
  // =====================================================================
  val dmem_write = Module(new PMEMWrite)
  dmem_write.clock := clock

  val dm_wr_idle :: dm_wr_resp :: Nil = Enum(2)
  val dmemWrState = RegInit(dm_wr_idle)

  // 锁存写请求
  val dmemWrAddrReg = Reg(UInt(Config.ADDR_WIDTH.W))
  val dmemWrDataReg = Reg(UInt(Config.XLEN.W))
  val dmemWrMaskReg = Reg(UInt(4.W))

  // PMEMWrite 默认不写
  dmem_write.wen   := false.B
  dmem_write.waddr := dmemWrAddrReg
  dmem_write.wdata := dmemWrDataReg
  dmem_write.wmask := dmemWrMaskReg

  io.dmem.aw.ready   := false.B
  io.dmem.w.ready    := false.B
  io.dmem.b.valid    := false.B
  io.dmem.b.bits.resp := 0.U

  switch(dmemWrState) {
    is(dm_wr_idle) {
      // 同时接受 AW 和 W (要求 LSU 同时拉高两者, 这在 LSU 状态机中保证)
      io.dmem.aw.ready := io.dmem.w.valid  // 只有 W 也 valid 时才 ready
      io.dmem.w.ready  := io.dmem.aw.valid // 只有 AW 也 valid 时才 ready

      when(io.dmem.aw.fire && io.dmem.w.fire) {
        dmemWrAddrReg := io.dmem.aw.bits.addr
        dmemWrDataReg := io.dmem.w.bits.data
        dmemWrMaskReg := io.dmem.w.bits.mask
        // 执行实际写入 (在下一个时钟沿 always @posedge 中由 PMEMWrite 完成)
        dmem_write.wen   := true.B
        dmem_write.waddr := io.dmem.aw.bits.addr
        dmem_write.wdata := io.dmem.w.bits.data
        dmem_write.wmask := io.dmem.w.bits.mask
        dmemWrState := dm_wr_resp
      }
    }
    is(dm_wr_resp) {
      io.dmem.b.valid := true.B
      io.dmem.b.bits.resp := 0.U  // OK
      when(io.dmem.b.fire) {
        dmemWrState := dm_wr_idle
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
