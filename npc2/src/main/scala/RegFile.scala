// MiniRV 寄存器堆
package minirv

import chisel3._
import chisel3.util._

/**
  * 32 个通用寄存器
  * x0 恒为 0
  */
class RegFile extends Module {
  val io = IO(new Bundle {
    // 两个读端口 (rs1, rs2)
    val rs1_addr = Input(UInt(Config.REG_ADDR_W.W))
    val rs2_addr = Input(UInt(Config.REG_ADDR_W.W))
    val rs1_data = Output(UInt(Config.XLEN.W))
    val rs2_data = Output(UInt(Config.XLEN.W))
    
    // 一个写端口 (rd)
    val rd_addr  = Input(UInt(Config.REG_ADDR_W.W))
    val rd_data  = Input(UInt(Config.XLEN.W))
    val rd_wen   = Input(Bool())
  })

  // 32 个 32 位寄存器.
  val regs = RegInit(VecInit(Seq.fill(32)(0.U(Config.XLEN.W))))

  // ========== Difftest: 将寄存器堆同步到 C++ 端 ==========
  val regSync = Module(new RegFileSync)
  regSync.io.clock := clock
  regSync.io.regs  := regs

  // 读取 (x0 恒为 0)
  io.rs1_data := Mux(io.rs1_addr === 0.U, 0.U, regs(io.rs1_addr))
  io.rs2_data := Mux(io.rs2_addr === 0.U, 0.U, regs(io.rs2_addr))

  // 写入 (x0 不可写)
  when(io.rd_wen && io.rd_addr =/= 0.U) {
    regs(io.rd_addr) := io.rd_data
  }
}

/**
  * CSR
  */
class CSRFile extends Module {
  val io = IO(new Bundle{
    // 软件读写引脚, 由csrrw/s/c指令触发.
    val csr_addr = Input(UInt(12.W))
    val csr_wdata = Input(UInt(32.W))
    val csr_wen = Input(Bool())
    val csr_rdata = Output(UInt(32.W))

    //硬件自动触发引脚
    val exception_en = Input(Bool()) // 发生异常时置位
    val exception_pc = Input(UInt(32.W)) // 案发现场的 PC
    val cause        = Input(UInt(32.W)) // 事故代码 
    val mtvec_out    = Output(UInt(32.W)) // 给 PC 跳转用
    val mepc_out     = Output(UInt(32.W)) // 给 mret 指令用
  })

  // 定义CSR寄存器. (索引csr有12bit, 也就是说最多有4096个CSR寄存器. 下面是基础的...)
  val mtvec   = RegInit(0.U(32.W))    // csr=0x305 machine trap vec, csr=存放异常入口地址. bit[1:0]是向量模式(现在我们固定一个跳转地址, 这两位不管置零)
  val mepc    = RegInit(0.U(32.W))    // csr=0x341 存放异常发生时的 PC
  val mcause  = RegInit(0.U(32.W))    // csr=0x342 存放异常原因
  //val mie     = RegInit(0.U(32.W))  // csr=0x304 machine interrupt enable, 中断使能
  //val mip     = RegInit(0.U(32.W))  // csr=0x344 machine interrupt pending, 中断挂起
  //val mtval   = RegInit(0.U(32.W))  // csr=0x343 machine trap value, 它保存了trap的附加信息：地址例外中出错的地址、发生非法指令例外的指令本身，对于其他异常，它的值为 0。
  //val scratch = RegInit(0.U(32.W))  // csr=0x340 一个临时寄存器, 用于 csrrw/s/c 指令交换数据
  val mstatus = RegInit(0.U(32.W))    // csr=0x300 存放状态. bit[3]是MIE, bit[12:11]是MPP

  //软件读写逻辑


  //硬件读写逻辑
}

