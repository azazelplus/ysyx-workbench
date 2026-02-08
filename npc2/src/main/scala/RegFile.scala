// MiniRV 寄存器堆
package minirv

import chisel3._
import chisel3.util._

/**
  * 32 个通用寄存器GPR
  * x0 恒为 0
  */
class GPRFile extends Module {
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

    // Difftest 同步输出端口: 将 32 个 GPR 值输出给顶层 RegFileSync
    val gpr_sync = Output(Vec(32, UInt(Config.XLEN.W)))
  })

  // 32 个 GPR 寄存器例化.
  val regs = RegInit(VecInit(Seq.fill(32)(0.U(Config.XLEN.W))))

  // 连接到读接口 (x0 恒为 0)
  io.rs1_data := Mux(io.rs1_addr === 0.U, 0.U, regs(io.rs1_addr))
  io.rs2_data := Mux(io.rs2_addr === 0.U, 0.U, regs(io.rs2_addr))

  // 连接到写接口 (x0 不可写)
  when(io.rd_wen && io.rd_addr =/= 0.U) {
    regs(io.rd_addr) := io.rd_data
  }

  // 连接32个GPR到同步输出端口
  io.gpr_sync := regs
}

/**
  * CSR寄存器
  */
class CSRFile extends Module {

  // 寄存器IO接口
  val io = IO(new Bundle{
    // 软件读写引脚, 由csrrw/s/c指令触发.
    val csr_addr = Input(UInt(12.W))    // csr指令的CSR地址索引 (12bit, CSR指令属于I type, 其中imm的12bit被用来存csr地址)
    val csr_wdata = Input(UInt(32.W))   // csr指令写CSR时的数据 (rs1或zimm)
    val csr_wen = Input(Bool())         // csr指令写CSR的使能 (由IDU根据指令类型生成)
    val csr_rdata = Output(UInt(32.W))  // csr指令读CSR时的数据 (由CSRFile根据csr_addr输出)

    // 硬件自动触发引脚
    val exception_en = Input(Bool())      // 发生异常时置位
    val exception_pc = Input(UInt(32.W))  // 案发现场的 PC
    val cause        = Input(UInt(32.W))  // 事故代码 
    val mtvec_out    = Output(UInt(32.W)) // 读出mtvec寄存器的值, 给 ecall 指令跳转用
    val mepc_out     = Output(UInt(32.W)) // 读出mepc寄存器的值, 给 mret 指令返回用
    val is_mret      = Input(Bool())      // MRET指令

    // 连接CSR到同步输出端口
    val csr_sync = Output(new CSRSyncBundle)
  })

  // ========== CSR 寄存器实例化. (索引csr有12bit, 也就是说最多有4096个CSR寄存器. 只实现基础的...) ==========
  // --- 可读写 CSR ---
  val mstatus = RegInit(0.U(32.W))    // 0x300 存放状态. bit[3]是MIE, bit[12:11]是MPP
  val mtvec   = RegInit(0.U(32.W))    // 0x305 machine trap vec, 存放异常入口地址
  val mepc    = RegInit(0.U(32.W))    // 0x341 存放异常发生时的 PC
  val mcause  = RegInit(0.U(32.W))    // 0x342 存放异常原因
  //val mie     = RegInit(0.U(32.W))  // 0x304 machine interrupt enable, 中断使能
  //val mip     = RegInit(0.U(32.W))  // 0x344 machine interrupt pending, 中断挂起
  //val mtval   = RegInit(0.U(32.W))  // 0x343 machine trap value
  //val mscratch = RegInit(0.U(32.W)) // 0x340 临时寄存器

  // --- 硬件自增 CSR ---
  // mcycle: 64位周期计数器, 在 RV32 中拆为 mcycle(低32位) 和 mcycleh(高32位)
  val mcycle_full = RegInit(0.U(64.W))
  mcycle_full := mcycle_full + 1.U                     // 每周期自增
  val mcycle  = mcycle_full(31, 0)                     // 0xB00 低32位
  val mcycleh = mcycle_full(63, 32)                    // 0xB80 高32位

  // --- 只读 CSR ---
  val mvendorid = "h79737978".U(32.W)                  // 0xF11 厂商标识
  val marchid   = "h17f270e".U(32.W)                   // 0xF12 架构标识

  // ========== 软件读写逻辑 ==========
  // csr读数据通路
  io.csr_rdata := MuxLookup(io.csr_addr, 0.U)(Seq(
    CSRAddr.mstatus   -> mstatus,
    CSRAddr.mtvec     -> mtvec,
    CSRAddr.mepc      -> mepc,
    CSRAddr.mcause    -> mcause,
    CSRAddr.mcycle    -> mcycle,
    CSRAddr.mcycleh   -> mcycleh,
    CSRAddr.mvendorid -> mvendorid,
    CSRAddr.marchid   -> marchid
  ))
  // 连接csr到写接口. 只读寄存器 (mvendorid, marchid) 不允许写入. mcycle/mcycleh 可由软件写入.
  when(io.csr_wen) {
    switch(io.csr_addr) {
      is(CSRAddr.mstatus) { mstatus := io.csr_wdata }
      is(CSRAddr.mtvec)   { mtvec   := io.csr_wdata }
      is(CSRAddr.mepc)    { mepc    := io.csr_wdata }
      is(CSRAddr.mcause)  { mcause  := io.csr_wdata }
      is(CSRAddr.mcycle)  { mcycle_full := Cat(mcycle_full(63, 32), io.csr_wdata) }
      is(CSRAddr.mcycleh) { mcycle_full := Cat(io.csr_wdata, mcycle_full(31, 0)) }
      // mvendorid, marchid 为只读, 写入无效
    }
  }

  io.mtvec_out := mtvec
  io.mepc_out  := mepc

  // ========== 硬件读写逻辑 (异常/中断处理) ==========
  // 当异常发生时(exception_en=1), 硬件自动更新 mepc, mcause, mstatus
  when(io.exception_en) {
    mepc   := io.exception_pc
    mcause := io.cause
    
    // mstatus update for trap:
    // 备份当前中断使能(MIE)到 MPIE, 设置 MPP=11(M-mode), 关闭中断(MIE=0)
    val mie = mstatus(3)
    mstatus := Cat(mstatus(31, 13), 3.U(2.W), mstatus(10, 8), mie, mstatus(6, 4), 0.U(1.W), mstatus(2, 0))
  }
  // For mret: 恢复 MPIE 到 MIE, MPIE 置 1
  when(io.is_mret) {
    val mpie = mstatus(7)
    mstatus := Cat(mstatus(31, 8), 1.U(1.W), mstatus(6, 4), mpie, mstatus(2, 0))
  }

  // ========== 连接CSR寄存器值到diff接口 ==========
  io.csr_sync.mstatus   := mstatus
  io.csr_sync.mtvec     := mtvec
  io.csr_sync.mepc      := mepc
  io.csr_sync.mcause    := mcause
  io.csr_sync.mcycle    := mcycle
  io.csr_sync.mcycleh   := mcycleh
  io.csr_sync.mvendorid := mvendorid
  io.csr_sync.marchid   := marchid
}

/**
  * CSR 同步数据 Bundle (用于 Difftest)
  */
class CSRSyncBundle extends Bundle {
  val mstatus   = UInt(32.W)
  val mtvec     = UInt(32.W)
  val mepc      = UInt(32.W)
  val mcause    = UInt(32.W)
  val mcycle    = UInt(32.W)
  val mcycleh   = UInt(32.W)
  val mvendorid = UInt(32.W)
  val marchid   = UInt(32.W)
}


