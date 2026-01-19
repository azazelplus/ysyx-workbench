// MiniRV 访存单元 (Load/Store Unit).
// 只有load&store指令会被LSU处理, 其他指令经过LSU只是为了去WBU.


/*
 * ┌──────────────┬──────────────┬──────────────┬──────────────┬──────────────────────────────────────┐
 * │ 指令类型      │  PMEM read   │  PMEM write  │    Reg WB    │ 数据流向                              │
 * ├──────────────┼──────────────┼──────────────┼──────────────┼──────────────────────────────────────┤
 * │ Load (lw)    │ ✅          │ ❌           │ ✅          │ PMEM -> LSU -> WBU -> RegFile        │
 * │ Store (sw)   │ ❌           │ ✅          │ ❌           │ RegFile(rs2) -> EXU -> LSU -> PMEM   │
 * │ 普通计算(add) │ ❌           │ ❌          │  ✅          │ ALU -> LSU -> WBU -> RegFile         │
 * └──────────────┴──────────────┴──────────────┴──────────────┴──────────────────────────────────────┘
 *
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
  * LSU - 访存单元
  * 
  * 功能：
  * 1. 通过外部存储器接口进行数据读写
  * 2. 处理不同宽度的访存 (lw/lbu/sw/sb)
  * 3. 生成写回数据
  */
class LSU extends Module {
  val io = IO(new Bundle {
    // ========== EXU->LSU ==========
    val in = Input(new EX2LS)   // 输入: EXU->LSU.   alu_result(alu计算结果), store_data(S指令要用的数据), rd_addr(WB用), mem_wen, mem_ren, mem_op, reg_wen

    // ========== LSU->WBU接口 ==========
    val out = Output(new LS2WB) // 输出: LSU->WBU, 最终写回数据/rd/reg_wen

    // ========== 数据存储器接口 LSU<->PMEM ==========
    val dmem = new DMemIO       // req: LSU->PMEM, resp: PMEM->LSU
  })



  val in = io.in
  val addr = in.alu_result  //addr: 对PMEM的读/写请求. 来自ALU计算结果. 对于Load/Store指令, alu-result就是要向DMem请求读/写的地址. 
  val byte_offset = addr(1, 0)  // alu_result对于 load/store 指令是 read/write 地址.  地址的低 2 位被用于选择字节.例如: 
// 如果请求读取raddr = 8000 0001, offset=01, 意味着要取8000 0000的第2个byte.
// 如果请求读取raddr = 8000 0002, offset=10, 意味着要取8000 0000的第3个byte.
// 如果请求读取raddr = 8000 0003, offset=11, 意味着要取8000 0000的第4个byte.
// 如果请求读取raddr = 8000 0004, offset=00, 意味着要取8000 0000的第1个byte.

// 如果请求写入waddr = 8000 0001, offset=01, 意味着要写8000 0000的第2个byte.


  // ============ 1.load指令实现, 连接PMEM read端: 处理output raddr, input rdata============

  // LSU透传了io.in.alu_result -> io.dmem.req.raddr. 这是因为, 只有load指令才会进行读操作, 此时alu_result就是要读的地址. 
  // 修正：PMEM 接口期望对齐的地址，因为 LSU 内部逻辑假设读取到的是包含目标字节的完整字
  io.dmem.req.raddr := Cat(addr(31, 2), 0.U(2.W))
  val rdata_raw = io.dmem.resp.rdata
  

  // 输入rdata, 根据 mem_op 和地址偏移, 提取正确的load_data.
  val load_data = WireDefault(0.U(Config.XLEN.W))
  
  switch(in.mem_op) {

    // load word: 
    is(MemOp.LW) {
      // Load Word: 直接使用 32 位数据
      load_data := rdata_raw
    }

    // load byte
    is(MemOp.LB) {
      // Load Byte (有符号扩展)
      val byte_data = MuxLookup(byte_offset, 0.U(8.W))(Seq(
        0.U -> rdata_raw(7, 0),
        1.U -> rdata_raw(15, 8),
        2.U -> rdata_raw(23, 16),
        3.U -> rdata_raw(31, 24)
      ))
      load_data := Cat(Fill(24, byte_data(7)), byte_data)  // 符号扩展
    }

    // load byte unsigned
    is(MemOp.LBU) {
      // Load Byte Unsigned (零扩展)
      val byte_data = MuxLookup(byte_offset, 0.U(8.W))(Seq(
        0.U -> rdata_raw(7, 0),
        1.U -> rdata_raw(15, 8),
        2.U -> rdata_raw(23, 16),
        3.U -> rdata_raw(31, 24)
      ))
      load_data := Cat(0.U(24.W), byte_data)  // 零扩展
    }

    // load halfword
    is(MemOp.LH) {
      // Load Halfword (有符号扩展)
      val half_data = Mux(byte_offset(1), rdata_raw(31, 16), rdata_raw(15, 0))
      load_data := Cat(Fill(16, half_data(15)), half_data)
    }

    // load halfword unsigned
    is(MemOp.LHU) {
      // Load Halfword Unsigned (零扩展)
      val half_data = Mux(byte_offset(1), rdata_raw(31, 16), rdata_raw(15, 0))
      load_data := Cat(0.U(16.W), half_data)
    }
  }



  // ============ 2.store指令实现, 连接PMEM write端: 处理input wen, waddr, wdata, wmask============
  io.dmem.req.wen   := in.mem_wen
  // 修正：PMEM 接口期望对齐的地址
  io.dmem.req.waddr := Cat(addr(31, 2), 0.U(2.W))
  
  // 根据 mem_op 和地址偏移生成写数据和写掩码
  val wdata = WireDefault(0.U(32.W))
  val wmask = WireDefault(0.U(4.W))
  
  switch(in.mem_op) {

    //store word
    is(MemOp.SW) {
      // Store Word
      wdata := in.store_data
      wmask := "b1111".U
    }

    //store halfword
    is(MemOp.SH) {
      // Store Halfword
      wdata := Mux(byte_offset(1),
        Cat(in.store_data(15, 0), 0.U(16.W)),
        Cat(0.U(16.W), in.store_data(15, 0))
      )
      wmask := Mux(byte_offset(1), "b1100".U, "b0011".U)
    }

    //store byte
    is(MemOp.SB) {
      // Store Byte
      wdata := MuxLookup(byte_offset, 0.U)(Seq(
        0.U -> Cat(0.U(24.W), in.store_data(7, 0)),
        1.U -> Cat(0.U(16.W), in.store_data(7, 0), 0.U(8.W)),
        2.U -> Cat(0.U(8.W), in.store_data(7, 0), 0.U(16.W)),
        3.U -> Cat(in.store_data(7, 0), 0.U(24.W))
      ))
      wmask := MuxLookup(byte_offset, 0.U)(Seq(
        0.U -> "b0001".U,
        1.U -> "b0010".U,
        2.U -> "b0100".U,
        3.U -> "b1000".U
      ))
    }
  }
  
  io.dmem.req.wdata := wdata
  io.dmem.req.wmask := wmask

  // ============ 3.输出到 WBU ============
  // 选择写回寄存器的数据. 此处区分load指令和非load的写回指令. Load 指令则为从内存读出来的结果load_data, 其他则为ALU的结果. 当reg_wen=1时说明当前指令要写入, 否则wb_data无意义.
  val wb_data = Mux(in.mem_ren, load_data, in.alu_result)

  io.out.wb_data := wb_data
  io.out.rd_addr := in.rd_addr
  io.out.reg_wen := in.reg_wen
}
