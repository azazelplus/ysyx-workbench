package core

import chisel3._
import chisel3.util._


/** 核心参数化配置类。
  *
  * 所有 CPU 内部设计参数均在此定义. 由 Top 层通过 implicit 自顶向下传递.
  * 各自
  */
case class CoreConfig(
    addrBits: Int,
    dataBits: Int,
    physRegs: Int,
    csrAddrBits: Int,
    entryAddr: Long,
    instIdBits: Int,
    ifOutstanding: Int,
    bhtEntries: Int,
    btbEntries: Int,
    rasEntries: Int,
    // RV32M 总开关：控制译码合法性与 MDU 路径启用。
    hasRVM: Boolean,
    enablePerfCnt: Boolean
) {
  val wordSize: Int = dataBits / 8
  val regAddrBits: Int = log2Up(physRegs)

  val addrWidth: Width = addrBits.W
  val dataWidth: Width = dataBits.W
  val instIdWidth: Width = instIdBits.W
  val regAddrWidth: Width = regAddrBits.W
  val csrAddrWidth: Width = csrAddrBits.W

  val EnablePerfCnt: Boolean = enablePerfCnt

  def entryAddress: UInt = entryAddr.U(addrWidth)
}
