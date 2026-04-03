// MiniRV 访存单元 (Load/Store Unit)
// Load/Store 指令通过 AXI-like 接口访问 DMEM, 其他指令透传到 WBU.

/*
 * ┌──────────────┬──────────────┬──────────────┬──────────────┬──────────────────────────────────────┐
 * │ 指令类型      │  PMEM read   │  PMEM write  │    Reg WB    │ 数据流向                              │
 * ├──────────────┼──────────────┼──────────────┼──────────────┼──────────────────────────────────────┤
 * │ Load (lw)    │ ✅          │ ❌           │ ✅          │ PMEM -> LSU -> WBU -> GPRFile        │
 * │ Store (sw)   │ ❌           │ ✅          │ ❌           │ GPRFile(rs2) -> EXU -> LSU -> PMEM   │
 * │ 普通计算(add) │ ❌           │ ❌          │  ✅          │ ALU -> LSU -> WBU -> GPRFile         │
 * └──────────────┴──────────────┴──────────────┴──────────────┴──────────────────────────────────────┘
 */

package minirv.lsu

import chisel3._
import chisel3.util._
import minirv._

/**
  * L/S指令区分: 内存操作类型 (funct3)
  */
object MemOp {
  val LB  = "b000".U(3.W)  // Load Byte (有符号扩展)
  val LH  = "b001".U(3.W)  // Load Halfword (有符号扩展)
  val LW  = "b010".U(3.W)  // Load Word
  val LBU = "b100".U(3.W)  // Load Byte Unsigned (零扩展)
  val LHU = "b101".U(3.W)  // Load Halfword Unsigned (零扩展)
  val SB  = "b000".U(3.W)  // Store Byte
  val SH  = "b001".U(3.W)  // Store Halfword
  val SW  = "b010".U(3.W)  // Store Word
}

/**
  * LSU - 访存单元 (多周期, 带 AXI DMEM 接口)
  *
  * 状态机:
  *   s_idle:      等待上游数据. io.in.ready = true.
  *   s_mem_read:  等待 DMEM R 通道返回读数据.
  *   s_mem_write: 等待 DMEM B 通道返回写响应.
  *   s_done:      计算完成, 等待下游接收结果. io.out.valid = true.
  */
class LSU extends Module {
  val io = IO(new Bundle {
    // ========== EXU->LSU ==========
    val in = Flipped(Decoupled(new EX2LS))

    // ========== LSU->WBU接口 ==========
    val out = Decoupled(new LS2WB)

    // ========== 数据存储器接口 LSU<->PMEM (AXI-Lite) ==========
    val axi = new AXILiteMasterIO
  })

  // ========== 状态机 ==========
  val s_idle :: s_mem_read :: s_mem_write :: s_done :: Nil = Enum(4)
  val state = RegInit(s_idle)

  // 锁存输入数据 (在 s_idle 接收后保持)
  val inReg = Reg(new EX2LS)

  // 锁存从 DMEM 读回的数据
  val rdataReg = Reg(UInt(Config.XLEN.W))

  // ========== 地址和字节偏移计算 (组合逻辑, 基于锁存的 inReg) ==========
  val addr = inReg.alu_result
  val byte_offset = addr(1, 0)
  val aligned_addr = Cat(addr(31, 2), 0.U(2.W))

  // ========== Store 数据/掩码生成 (组合逻辑) ==========
  val wdata = WireDefault(0.U(32.W))
  val wmask = WireDefault(0.U(4.W))

  switch(inReg.mem_op) {
    is(MemOp.SW) {
      wdata := inReg.store_data
      wmask := "b1111".U
    }
    is(MemOp.SH) {
      wdata := Mux(byte_offset(1),
        Cat(inReg.store_data(15, 0), 0.U(16.W)),
        Cat(0.U(16.W), inReg.store_data(15, 0))
      )
      wmask := Mux(byte_offset(1), "b1100".U, "b0011".U)
    }
    is(MemOp.SB) {
      wdata := MuxLookup(byte_offset, 0.U)(Seq(
        0.U -> Cat(0.U(24.W), inReg.store_data(7, 0)),
        1.U -> Cat(0.U(16.W), inReg.store_data(7, 0), 0.U(8.W)),
        2.U -> Cat(0.U(8.W), inReg.store_data(7, 0), 0.U(16.W)),
        3.U -> Cat(inReg.store_data(7, 0), 0.U(24.W))
      ))
      wmask := MuxLookup(byte_offset, 0.U)(Seq(
        0.U -> "b0001".U,
        1.U -> "b0010".U,
        2.U -> "b0100".U,
        3.U -> "b1000".U
      ))
    }
  }

  // ========== Load 数据提取 (组合逻辑, 基于锁存的 rdataReg) ==========
  val load_data = WireDefault(0.U(Config.XLEN.W))

  switch(inReg.mem_op) {
    is(MemOp.LW) {
      load_data := rdataReg
    }
    is(MemOp.LB) {
      val byte_data = MuxLookup(byte_offset, 0.U(8.W))(Seq(
        0.U -> rdataReg(7, 0),
        1.U -> rdataReg(15, 8),
        2.U -> rdataReg(23, 16),
        3.U -> rdataReg(31, 24)
      ))
      load_data := Cat(Fill(24, byte_data(7)), byte_data)
    }
    is(MemOp.LBU) {
      val byte_data = MuxLookup(byte_offset, 0.U(8.W))(Seq(
        0.U -> rdataReg(7, 0),
        1.U -> rdataReg(15, 8),
        2.U -> rdataReg(23, 16),
        3.U -> rdataReg(31, 24)
      ))
      load_data := Cat(0.U(24.W), byte_data)
    }
    is(MemOp.LH) {
      val half_data = Mux(byte_offset(1), rdataReg(31, 16), rdataReg(15, 0))
      load_data := Cat(Fill(16, half_data(15)), half_data)
    }
    is(MemOp.LHU) {
      val half_data = Mux(byte_offset(1), rdataReg(31, 16), rdataReg(15, 0))
      load_data := Cat(0.U(16.W), half_data)
    }
  }

  // ========== 写回数据选择 ==========
  val wb_data = Mux(inReg.mem_ren, load_data, inReg.alu_result)

  // ========== 默认输出 ==========
  io.in.ready  := false.B
  io.out.valid := false.B
  io.out.bits.inst_valid := inReg.inst_valid  // 透传 inst_valid
  io.out.bits.pc      := inReg.pc
  io.out.bits.inst    := inReg.inst
  io.out.bits.wb_data := wb_data
  io.out.bits.rd_addr := inReg.rd_addr
  io.out.bits.reg_wen := inReg.reg_wen

  // AXI 通道默认值 (全部不发)
  io.axi.ar.valid     := false.B
  io.axi.ar.bits.addr := aligned_addr
  io.axi.r.ready      := false.B
  io.axi.aw.valid     := false.B
  io.axi.aw.bits.addr := aligned_addr
  io.axi.w.valid      := false.B
  io.axi.w.bits.data  := wdata
  io.axi.w.bits.strb  := wmask
  io.axi.b.ready      := false.B

  // ========== 状态机逻辑 ==========
  switch(state) {
    is(s_idle) {
      io.in.ready := true.B
      when(io.in.fire) {
        inReg := io.in.bits
        // 无效指令: 直接跳到 s_done, 不执行内存操作
        when(!io.in.bits.inst_valid) {
          state := s_done
        }.elsewhen(io.in.bits.mem_ren) {
          state := s_mem_read
        }.elsewhen(io.in.bits.mem_wen) {
          state := s_mem_write
        }.otherwise {
          state := s_done
        }
      }
    }

    is(s_mem_read) {
      // 发送 AR 请求
      io.axi.ar.valid     := true.B
      io.axi.ar.bits.addr := aligned_addr

      // 同时准备接收 R 响应 (PMEM 是 1-cycle SRAM, AR.fire 后下一周期 R.valid 拉高)
      io.axi.r.ready := true.B
      when(io.axi.r.fire) {
        rdataReg := io.axi.r.bits.data
        state := s_done
      }
    }

    is(s_mem_write) {
      // 同时发送 AW 和 W
      io.axi.aw.valid     := true.B
      io.axi.aw.bits.addr := aligned_addr
      io.axi.w.valid      := true.B
      io.axi.w.bits.data  := wdata
      io.axi.w.bits.strb  := wmask

      // 等待 B 响应
      io.axi.b.ready := true.B
      when(io.axi.b.fire) {
        state := s_done
      }
    }

    is(s_done) {
      io.out.valid := true.B
      when(io.out.fire) {
        state := s_idle
      }
    }
  }
}
