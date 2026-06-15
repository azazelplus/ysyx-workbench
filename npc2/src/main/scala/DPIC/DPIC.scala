// DPI-C 存储器访问接口
// 通过 DPI-C 机制与 C++ 仿真环境交互.
// PMEMRead内存访问接口 input: raddr  output: rdata
// PMEMWrite内存写入接口 input: wen, waddr, wdata,
// EBREAKDetect EBREAK指令检测接口 input: inst, valid





// DPI-C(direct programming interface-c) 是一种用于将system verilog和c/c++代码进行交互的技术.
// 它允许verilog代码直接调用c/c++函数，反之亦然.
// 它允许你在 Verilog 代码里直接写 pmem_read(...)，然后仿真器（比如 Verilator）在运行时，会跳出去执行你写好的 C++ 函数，拿回结果后再跳回硬件世界.




package azazerv

import chisel3._
import chisel3.util._
// ExtModule 是 Chisel 中用于定义外部模块接口的类. 通过继承 ExtModule, 我们可以定义一个与外部模块（如 DPI-C 模块）交互的接口. 这个模块本身不包含任何实现细节, 只是声明了输入输出端口. 实际的功能实现是在外部（如 SystemVerilog 或 C++）中完成的.

/**
  * 内存模块(DPI-C 存储器读取模块) Phy-MEM. 组合瞬间访问.
  * 
  * 使用 Chisel 的 BlackBox 机制，定义外部 DPI-C 函数接口。
  * 实际的函数实现在 C++ 代码中。
  * 
  * pmem_read: 从存储器读取 32 位数据
  *   - raddr: 读地址（需按 4 字节对齐）
  *   - 返回: 32 位数据
  */
class PMEMRead extends ExtModule {
  val clock  = IO(Input(Clock()))
  val raddr  = IO(Input(UInt(32.W)))   // 读地址
  val rdata  = IO(Output(UInt(32.W)))  // 读数据
  addResource("/PMEMRead.sv")
}


/**
  * DPI-C 存储器写入模块
  * 
  * pmem_write: 向存储器写入数据
  *   - waddr: 写地址（需按 4 字节对齐）
  *   - wdata: 写数据
  *   - wmask: 写掩码(按字节，4 位)
  */
class PMEMWrite extends ExtModule {
  val clock  = IO(Input(Clock()))
  val wen    = IO(Input(Bool()))
  val waddr  = IO(Input(UInt(32.W)))
  val wdata  = IO(Input(UInt(32.W)))
  val wmask  = IO(Input(UInt(4.W)))
  addResource("/PMEMWrite.sv")
}

/**
  * EBREAK 检测模块
  * 用于在仿真中检测 EBREAK 指令并终止仿真
  */
class EBREAKDetect extends ExtModule {
  val clock  = IO(Input(Clock()))
  val inst   = IO(Input(UInt(32.W)))
  val valid  = IO(Input(Bool()))
  addResource("/EBREAKDetect.sv")
}

/**
  * 寄存器堆同步模块（标准 DPI-C 接口，仿真器无关）
  *
  * 通过标准 DPI-C 接口将寄存器值同步到 C++ 环境，支持 SDB/DiffTest 等功能。
  *
  * 【架构设计】
  * - 输入：GPR[32] + CSR[8]，每周期从硬件读取
  * - 输出：无（纯粹的数据导出模块）
  * - 接口：标准 DPI-C（支持 Verilator/VCS/Questa/Cadence）
  *
  * 【性能说明】
  * - 当 ENABLE_SDB=0 且 ENABLE_DIFFTEST=0 时，C 函数编译为空桩（零开销）
  * - 硬件综合时会自动忽略此模块（DPI-C 仅用于仿真）
  */
class RegFileSync extends ExtModule {
  val clock          = IO(Input(Clock()))
  val gpr            = IO(Input(Vec(32, UInt(32.W))))
  val csr_mstatus    = IO(Input(UInt(32.W)))
  val csr_mtvec      = IO(Input(UInt(32.W)))
  val csr_mepc       = IO(Input(UInt(32.W)))
  val csr_mcause     = IO(Input(UInt(32.W)))
  val csr_mcycle     = IO(Input(UInt(32.W)))
  val csr_mcycleh    = IO(Input(UInt(32.W)))
  val csr_mvendorid  = IO(Input(UInt(32.W)))
  val csr_marchid    = IO(Input(UInt(32.W)))
  addResource("/RegFileSync.sv")
}

