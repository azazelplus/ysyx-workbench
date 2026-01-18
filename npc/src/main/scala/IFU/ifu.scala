// MiniRV 取指单元 (Instruction Fetch Unit)
package minirv.ifu

import chisel3._
import chisel3.util._
import minirv._

/**
  * IFU - 取指单元
  * 
  * 功能：
  * 1. 使用 PC 模块获取当前指令地址
  * 2. 通过外部存储器接口读取指令
  * 3. 将 PC 和指令传递给 IDU
  */
class IFU extends Module {
  val io = IO(new Bundle {
    // ========== IFU->IDU ==========
    val out = Output(new IF2ID)           // 输出: IFU->IDU, 传递取到的(pc, inst)

    // ========== EXU->IFU (控制冒险重定向) ==========
    val jump_en   = Input(Bool())                     // EXU->IFU, 是否跳转/分支成立
    val jump_addr = Input(UInt(Config.ADDR_WIDTH.W))  // EXU->IFU, 跳转目标地址

    // ========== MiniRV(流水线控制)->IFU ==========
    val stall     = Input(Bool())       // 暂停信号. MiniRV->IFU, Load-Use 冒险时冻结 PC/IF

    // ========== IFU<->PMEM (取指) ==========
    val imem_addr  = Output(UInt(32.W)) // 指令地址. IFU->PMEM, 输出当前 PC 给 PMEM
    val imem_rdata = Input(UInt(32.W))  // 指令数据. PMEM->IFU, 返回读取到的 32 位指令
  })

  // 实例化 PC 模块
  val pc_module = Module(new PC)
  
  // 连接 PC 模块的控制信号
  pc_module.io.jump_en   := io.jump_en
  pc_module.io.jump_addr := io.jump_addr
  pc_module.io.stall     := io.stall

  // 输出指令地址到外部存储器
  io.imem_addr := pc_module.io.pc

  // 输出到 IDU
  io.out.pc   := pc_module.io.pc
  io.out.inst := io.imem_rdata
}
