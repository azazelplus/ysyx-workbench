package core

import chisel3._
import chisel3.util._


/** 核心参数化配置类。
  *
  * 所有 CPU 内部设计参数均在此定义. 由 Top 层通过 implicit 自顶向下传递.
  */
case class CoreConfig(
    addrBits: Int = 32,                               // 地址和 PC 的位宽
    dataBits: Int = 32,                               // 数据与GPR的位宽
    physRegs: Int = 32,                               // GPR数量
    csrAddrBits: Int = 12,                            // CSR 地址位宽
    entryAddr: Long = 0x80000000L,                    // PC 复位地址
    instIdBits: Int = 1,                              // 指令 ID 位宽；当前执行路径未使用
    ifOutstanding: Int = 1,                           // 预留：IFU 可同时发出的未完成取指请求数
    bhtEntries: Int = 0,                              // 预留：分支历史表（BHT）条目数
    btbEntries: Int = 0,                              // 预留：分支目标缓冲（BTB）条目数
    rasEntries: Int = 0,                              // 预留：返回地址栈（RAS）深度

    // 功能开关
    hasRVM: Boolean = true,                           // 是否启用 RV32M 乘除法扩展
    enablePerfCnt: Boolean = false,                   // 预留：性能计数器开关；当前未接入
    fastMul: Boolean = true                           // 是否使用快速乘法路径
) {
  // 断言检查. 目前仅支持RV32.
  require(dataBits == 32 && addrBits == 32 && physRegs == 32 && csrAddrBits == 12,
    "AzazeRV currently requires RV32 with 32 GPRs and 12-bit CSR addresses")

  val instBits: Int = 32                              // 指令位宽
  val wordSize: Int = dataBits / 8                    // 一个数据字包含的字节数，RV32 时为 4
  val regAddrBits: Int = log2Up(physRegs)             // 寄存器编号所需位数，32 个寄存器时为 5

  val addrWidth: Width = addrBits.W                   // 将 addrBits 转为 Chisel 的 Width
  val dataWidth: Width = dataBits.W                   // 将 dataBits 转为 Chisel 的 Width
  val instIdWidth: Width = instIdBits.W               // 将 instIdBits 转为 Chisel 的 Width；当前未使用
  val regAddrWidth: Width = regAddrBits.W             // 将 regAddrBits 转为 Chisel 的 Width
  val csrAddrWidth: Width = csrAddrBits.W             // 将 csrAddrBits 转为 Chisel 的 Width；当前未使用

  val EnablePerfCnt: Boolean = enablePerfCnt          // enablePerfCnt 的兼容别名；当前未使用

  def entryAddress: UInt = entryAddr.U(addrWidth)     // 将复位地址转为指定宽度的 UInt，供 PC 初始化
}
