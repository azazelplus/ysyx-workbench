// MiniRV 访存单元 (Load/Store Unit)
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
  val addr = in.alu_result
  val byte_offset = addr(1, 0)  // 地址的低 2 位，用于选择字节

  // ============ 数据存储器读取 ============

  // LSU透传了io.in.alu_result -> io.dmem.req.raddr. 这是因为, 只有load 
  io.dmem.req.raddr := addr
  val rdata_raw = io.dmem.resp.rdata
  
  // 根据 mem_op 和地址偏移提取正确的数据
  val load_data = WireDefault(0.U(Config.XLEN.W))
  
  switch(in.mem_op) {
    is(MemOp.LW) {
      // Load Word: 直接使用 32 位数据
      load_data := rdata_raw
    }
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
    is(MemOp.LH) {
      // Load Halfword (有符号扩展)
      val half_data = Mux(byte_offset(1), rdata_raw(31, 16), rdata_raw(15, 0))
      load_data := Cat(Fill(16, half_data(15)), half_data)
    }
    is(MemOp.LHU) {
      // Load Halfword Unsigned (零扩展)
      val half_data = Mux(byte_offset(1), rdata_raw(31, 16), rdata_raw(15, 0))
      load_data := Cat(0.U(16.W), half_data)
    }
  }

  // ============ 数据存储器写入 ============
  io.dmem.req.wen   := in.mem_wen
  io.dmem.req.waddr := addr
  
  // 根据 mem_op 和地址偏移生成写数据和写掩码
  val wdata = WireDefault(0.U(32.W))
  val wmask = WireDefault(0.U(4.W))
  
  switch(in.mem_op) {
    is(MemOp.SW) {
      // Store Word
      wdata := in.store_data
      wmask := "b1111".U
    }
    is(MemOp.SH) {
      // Store Halfword
      wdata := Mux(byte_offset(1),
        Cat(in.store_data(15, 0), 0.U(16.W)),
        Cat(0.U(16.W), in.store_data(15, 0))
      )
      wmask := Mux(byte_offset(1), "b1100".U, "b0011".U)
    }
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

  // ============ 输出到 WBU ============
  // 选择写回数据：Load 指令从内存读，其他从 ALU
  val wb_data = Mux(in.mem_ren, load_data, in.alu_result)

  io.out.wb_data := wb_data
  io.out.rd_addr := in.rd_addr
  io.out.reg_wen := in.reg_wen
}
