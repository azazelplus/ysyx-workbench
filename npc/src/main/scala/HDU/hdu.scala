// MiniRV 冒险检测单元 (Hazard Detection Unit)
package minirv.hdu

import chisel3._
import chisel3.util._
import minirv._

/**
  * HDU - 冒险检测单元
  * 
  * 功能：
  * 1. Load-Use 冒险检测
  * 2. 控制冒险检测（分支/跳转）
  * 3. 生成 Stall 和 Flush 信号
  */
class HDU extends Module {
  val io = IO(new Bundle {
    // ========== 来自 ID 阶段的信号 ==========
    val id_rs1_addr = Input(UInt(Config.REG_ADDR_W.W))  // 当前 ID 阶段指令的 rs1 地址
    val id_rs2_addr = Input(UInt(Config.REG_ADDR_W.W))  // 当前 ID 阶段指令的 rs2 地址
    
    // ========== 来自 ID/EX 寄存器的信号 ==========
    val id_ex_mem_ren = Input(Bool())                   // ID/EX 阶段指令是否为 Load
    val id_ex_rd_addr = Input(UInt(Config.REG_ADDR_W.W)) // ID/EX 阶段指令的目标寄存器地址
    
    // ========== 来自 EX 阶段的信号 ==========
    val ex_jump_en = Input(Bool())                      // EX 阶段是否发生跳转
    
    // ========== 输出控制信号 ==========
    val stall = Output(Bool())                          // Stall 信号：暂停 IF 和 ID 阶段
    val flush = Output(Bool())                          // Flush 信号：清空 IF/ID 和 ID/EX 寄存器
  })

  // ============================================================
  // Load-Use 冒险检测
  // ============================================================
  // Load-Use 冒险：ID/EX 阶段是 Load 指令，且其 rd 是当前 ID 阶段的 rs1 或 rs2
  // mem_ren 信号就是 is_load 信号透传。
  // 在当前周期，id_ex 中的信号是上一周期 ID 阶段的信号，也就是上一条指令的信息。
  val load_use_hazard = io.id_ex_mem_ren && 
                        (io.id_ex_rd_addr =/= 0.U) &&
                        ((io.id_ex_rd_addr === io.id_rs1_addr) || 
                         (io.id_ex_rd_addr === io.id_rs2_addr))

  // ============================================================
  // 控制冒险检测
  // ============================================================
  // 当分支/跳转在 EX 阶段确定发生时，IF 和 ID 阶段已经取进了错误的指令，需要 Flush

  // ============================================================
  // 输出信号
  // ============================================================
  // Stall 信号：Load-Use 冒险时暂停 IF 和 ID 阶段
  io.stall := load_use_hazard
  
  // Flush 信号：分支/跳转发生时清空 IF/ID 和 ID/EX 阶段
  io.flush := io.ex_jump_en
}
