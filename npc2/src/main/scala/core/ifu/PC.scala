// AzazeRV 程序计数器 (Program Counter)
package azazerv.ifu

import chisel3._
import chisel3.util._
import azazerv._

/**
  * PC - 程序计数器
  *
  * 功能：
  * 1. 维护当前 PC 值
  * 2. 根据控制信号更新 PC (顺序 +4 或跳转)
  */
class PC extends Module {
  val io = IO(new Bundle {
    // PC 更新控制
    val jump_en   = Input(Bool())                       // 跳转使能
    val jump_addr = Input(UInt(Config.ADDR_WIDTH.W))    // 跳转目标地址
    val pc_en     = Input(Bool())                       // PC 推进使能: 由IFU和IDU握手信号驱动 ( := io.out.fire)

    // 当前 PC 输出
    val pc        = Output(UInt(Config.ADDR_WIDTH.W))   // 当前 PC 值
  })

  // PC 寄存器，初始值为 0x80000000 (RISC-V 典型复位地址)
  val pc_reg = RegInit("h80000000".U(Config.ADDR_WIDTH.W))
  io.pc := pc_reg

  // PC 推进:
  //   1. 顺序执行: 当上层握手成功时 (IDU 消费了本条指令), PC += 4
  //   2. 跳转: 当 jump_en 时, 无条件跳转到 jump_addr (优先级高于顺序执行)
  when(io.jump_en) {
    pc_reg := io.jump_addr
  }.elsewhen(io.pc_en) {
    pc_reg := pc_reg + 4.U
  }
}
