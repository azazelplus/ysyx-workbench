
/*
AzazeRV 访存单元 (Load/Store Unit)
Load/Store 指令通过 AXI-like 接口访问 DMEM, 其他指令透传到 WBU.

接收EXU发来的访存请求, 锁存它, 然后通过AXI4去mem读写, 拿到结果交给WBU. 非方寸指令直接透传.
 */

package azazerv.lsu

import chisel3._
import _root_.core.CoreConfig
import chisel3.util._
import azazerv._
import azazerv.idu.MemSize
import bus.AXI4MasterBus

/**
  * LSU - 访存单元 (多周期, 带 AXI DMEM 接口)
  *
  * 状态机只区分访存方向：
  *   s_idle: 没有进行中的访存；输出缓冲为空时接收上游指令。
  *   s_read: 通过 AR 发送读请求，等待 R 返回数据。
  *   s_write: 通过 AW / W 发送写请求，等待 B 返回响应。
  * 请求是否尚未被接收由各通道 valid 保存；完成结果由 out_valid 保持到下游接收。
  */
class LSU(implicit val cfg: CoreConfig) extends Module {
  val io = IO(new Bundle {
    // ========== EXU->LSU ==========
    val in = Flipped(Decoupled(new EX2LS))

    // ========== LSU->WBU接口 ==========
    val out = Decoupled(new LS2WB)

    // ========== 数据存储器接口 LSU<->PMEM (AXI4) ==========
    val axi = new AXI4MasterBus(cfg.bus)
  })

  // LSU 内部寄存器
  val inReg = Reg(new EX2LS)  // 锁存exu输入数据 (在 s_idle 接收后保持)
  val rdataReg = Reg(UInt(cfg.dataBits.W))  // 锁存读回的 32 位数据，再按 IDU 的宽度和扩展控制处理。
  val s_idle :: s_read :: s_write :: Nil = Enum(3)
  val state = RegInit(s_idle)

  // 每个 valid 寄存器直接表示该通道是否有待接收的请求
  // 地址、数据由 inReg 保持稳定，valid 在对应 fire 后清除
  val ar_valid = RegInit(false.B)   // 置1: 有个读地址准备好了, mem还没接收
  val aw_valid = RegInit(false.B)   // 置1: 有个写地址准备好了, mem还没接收
  val w_valid  = RegInit(false.B)   // 置1: 有个写数据准备好了, mem还没接收
  val out_valid = RegInit(false.B)  // 对WBU握手的valid打一拍. 置1: 有个结果准备好了, WBU还没接收.  此时将保持结果并阻止新指令覆盖 inReg. 它的作用其实等同于替换了s_done(结果都拿到了, 下一个上升沿可以输出了)这个状态.

  // 地址和字节偏移计算
  val addr = inReg.alu_result
  val byte_offset = addr(1, 0)    //lw/sw指令为0, lh/lhu/sh指令为0/2, lb/lbu/sb指令为0/1/2/3.
  val aligned_addr = Cat(addr(31, 2), 0.U(2.W)) // LSU按4byte对齐地址访问mem.

  // Store 数据/掩码生成. 对于非lw/sw, 需要把要写入的值摆到32bit字的正确偏移位置, 同时生成掩码信号给wbu写入.
  val wdata = WireDefault(0.U(32.W))
  val wmask = WireDefault(0.U(4.W))
  switch(inReg.mem_size) {
    is(MemSize.word) {
      wdata := inReg.store_data
      wmask := "b1111".U
    }
    is(MemSize.half) {
      wdata := Mux(byte_offset(1),
        Cat(inReg.store_data(15, 0), 0.U(16.W)),
        Cat(0.U(16.W), inReg.store_data(15, 0))
      )
      wmask := Mux(byte_offset(1), "b1100".U, "b0011".U)
    }
    is(MemSize.byte) {
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

  // Load 数据提取. 从原始数据中挑出正确字节/半字, 再做符号/0扩展.
  val load_data = WireDefault(0.U(cfg.dataBits.W))
  switch(inReg.mem_size) {
    is(MemSize.word) {
      load_data := rdataReg
    }
    is(MemSize.byte) {
      val byte_data = MuxLookup(byte_offset, 0.U(8.W))(Seq(
        0.U -> rdataReg(7, 0),
        1.U -> rdataReg(15, 8),
        2.U -> rdataReg(23, 16),
        3.U -> rdataReg(31, 24)
      ))
      val sign = !inReg.load_unsigned && byte_data(7)
      load_data := Cat(Fill(24, sign), byte_data)
    }
    is(MemSize.half) {
      val half_data = Mux(byte_offset(1), rdataReg(31, 16), rdataReg(15, 0))
      val sign = !inReg.load_unsigned && half_data(15)
      load_data := Cat(Fill(16, sign), half_data)
    }
  }

  // 给WBU的数据选择. load指令用load_data, store或普通指令则用alu_result透传.
  val wb_data = Mux(inReg.mem_ren, load_data, inReg.alu_result)

  // 绑定默认端口输出
  io.in.ready  := (state === s_idle) && !out_valid
  io.out.valid := out_valid
  io.out.bits.inst_valid := inReg.inst_valid  // 透传 inst_valid
  io.out.bits.pc      := inReg.pc
  io.out.bits.inst    := inReg.inst
  io.out.bits.wb_data := wb_data
  io.out.bits.rd_addr := inReg.rd_addr
  io.out.bits.reg_wen := inReg.reg_wen
  // AXI 请求通道：valid 表示待发送请求，payload 在整个事务期间保持稳定。
  // 当前 LSU 仍按对齐的 32 位字单拍访问；子字写入由 WSTRB 选择字节。
  // ID 固定为 0，地址通道的 LEN=0、SIZE=2、BURST=INCR。
  io.axi.ar.valid     := ar_valid
  io.axi.ar.bits      := 0.U.asTypeOf(io.axi.ar.bits)
  io.axi.ar.bits.addr := aligned_addr
  io.axi.ar.bits.len  := 0.U
  io.axi.ar.bits.size := 2.U
  io.axi.ar.bits.burst := 1.U
  io.axi.r.ready      := false.B
  io.axi.aw.valid     := aw_valid
  io.axi.aw.bits      := 0.U.asTypeOf(io.axi.aw.bits)
  io.axi.aw.bits.addr := aligned_addr
  io.axi.aw.bits.len  := 0.U
  io.axi.aw.bits.size := 2.U
  io.axi.aw.bits.burst := 1.U
  io.axi.w.valid      := w_valid
  io.axi.w.bits       := 0.U.asTypeOf(io.axi.w.bits)
  io.axi.w.bits.data  := wdata
  io.axi.w.bits.strb  := wmask
  io.axi.w.bits.last  := true.B
  io.axi.b.ready      := false.B

  when(io.axi.r.fire) {
    assert(io.axi.r.bits.last && io.axi.r.bits.id === 0.U,
      "LSU expects one R beat with ID 0")
  }
  when(io.axi.b.fire) {
    assert(io.axi.b.bits.id === 0.U, "LSU expects B with ID 0")
  }

  // 各通道握手
  when(io.axi.ar.fire) { ar_valid := false.B }
  when(io.axi.aw.fire) { aw_valid := false.B }
  when(io.axi.w.fire)  { w_valid  := false.B }
  when(io.out.fire)   { out_valid := false.B }

  // **状态机逻辑**
  switch(state) {
    is(s_idle) {
      when(io.in.fire) {
        inReg := io.in.bits
        when(!io.in.bits.inst_valid) {
          out_valid := true.B // 无效指令直接向下游传递，不执行访存
        }.elsewhen(io.in.bits.mem_ren) {
          ar_valid := true.B  // load指令, 发AR请求, 进入s_read状态
          state := s_read
        }.elsewhen(io.in.bits.mem_wen) {
          aw_valid := true.B  // store指令, 发AW请求, 进入s_write状态
          w_valid  := true.B
          state := s_write
        }.otherwise {
          out_valid := true.B // 普通计算指令, 直接向下游传递
        }
      }
    }
    // 处理load指令
    is(s_read) {
      io.axi.r.ready := true.B  //和ar_valid同拍打开r_ready等待slave.
      when(io.axi.r.fire) { //R握手, 从mem拿到data, load指令实现.
        rdataReg := io.axi.r.bits.data  
        out_valid := true.B // 等待wbu握手
        state := s_idle
      } 
      
    }
    // 处理store指令
    is(s_write) {
      io.axi.b.ready := true.B  // 等待slave回应B
      when(io.axi.b.fire) {
        out_valid := true.B
        state := s_idle
      }
    }
  }
}
