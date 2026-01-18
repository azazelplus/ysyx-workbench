// MiniRV 数据前递单元 (Forwarding Unit)
package minirv.fwu

import chisel3._
import chisel3.util._
import minirv._

/**
  * FWU - 数据前递单元
  * 
  * 功能：
  * 1. 检测数据依赖
  * 2. 从 EX/MEM 或 MEM/WB 阶段前递数据到 ID 阶段
  * 3. 输出前递后的 rs1_data 和 rs2_data
  */
class FWU extends Module {
  val io = IO(new Bundle {
    // ========== 来自 ID 阶段的信号 ==========
    val id_rs1_addr = Input(UInt(Config.REG_ADDR_W.W))  // 当前 ID 阶段指令的 rs1 地址
    val id_rs2_addr = Input(UInt(Config.REG_ADDR_W.W))  // 当前 ID 阶段指令的 rs2 地址
    val id_rs1_data_raw = Input(UInt(Config.XLEN.W))    // 寄存器堆读出的 rs1 原始值
    val id_rs2_data_raw = Input(UInt(Config.XLEN.W))    // 寄存器堆读出的 rs2 原始值
    
    // ========== 来自 EX 阶段组合逻辑输出 (前递源 A - 最高优先级) ==========
    val id_ex_rd_addr = Input(UInt(Config.REG_ADDR_W.W))  // ID/EX 阶段指令的目标寄存器地址 (当前正在 EX 执行)
    val id_ex_rd_data = Input(UInt(Config.XLEN.W))        // EX 阶段的 ALU 结果 (组合逻辑输出)
    val id_ex_reg_wen = Input(Bool())                     // ID/EX 阶段指令是否写寄存器
    val id_ex_mem_ren = Input(Bool())                     // ID/EX 阶段指令是否为 Load (Load-Use 无法前递)
    
    // ========== 来自 EX/MEM 寄存器的信号 (前递源 B) ==========
    val ex_mem_rd_addr = Input(UInt(Config.REG_ADDR_W.W)) // EX/MEM 阶段指令的目标寄存器地址
    val ex_mem_rd_data = Input(UInt(Config.XLEN.W))       // EX/MEM 阶段的 ALU 结果
    val ex_mem_reg_wen = Input(Bool())                    // EX/MEM 阶段指令是否写寄存器
    
    // ========== 来自 MEM/WB 寄存器的信号 (前递源 C) ==========
    val mem_wb_rd_addr = Input(UInt(Config.REG_ADDR_W.W)) // MEM/WB 阶段指令的目标寄存器地址
    val mem_wb_rd_data = Input(UInt(Config.XLEN.W))       // MEM/WB 阶段的写回数据
    val mem_wb_reg_wen = Input(Bool())                    // MEM/WB 阶段指令是否写寄存器
    
    // ========== 输出前递后的数据 ==========
    val rs1_data_fwd = Output(UInt(Config.XLEN.W))        // 前递后的 rs1 数据
    val rs2_data_fwd = Output(UInt(Config.XLEN.W))        // 前递后的 rs2 数据
  })

  // ============================================================
  // 计算前递后的 rs1_data
  // ============================================================
  // 优先级：A (EX - 组合逻辑) > B (EX/MEM) > C (MEM/WB) > D (寄存器堆原值)
  // 
  // 关键修复：EX 阶段的结果在组合逻辑中已经产生，但还没有锁存到 ex_mem_reg。
  // 当后续指令在 ID 阶段需要这个结果时，必须从 EX 阶段的组合逻辑输出前递。
  // 注意：如果 EX 阶段是 Load 指令，则无法前递（需要等到 MEM 阶段才有数据），
  //       这种情况由 HDU 检测并 stall。
  
  // 前递条件 A：EX 阶段指令要写寄存器、不是 Load、rd 不为 0、rd 匹配
  val fwd_ex_rs1 = io.id_ex_reg_wen && !io.id_ex_mem_ren && 
                   (io.id_ex_rd_addr =/= 0.U) && (io.id_ex_rd_addr === io.id_rs1_addr)
  
  io.rs1_data_fwd := MuxCase(io.id_rs1_data_raw, Seq(
    fwd_ex_rs1 -> io.id_ex_rd_data,
    (io.ex_mem_reg_wen && (io.ex_mem_rd_addr =/= 0.U) && (io.ex_mem_rd_addr === io.id_rs1_addr)) -> io.ex_mem_rd_data,
    (io.mem_wb_reg_wen && (io.mem_wb_rd_addr =/= 0.U) && (io.mem_wb_rd_addr === io.id_rs1_addr)) -> io.mem_wb_rd_data
  ))

  // ============================================================
  // 计算前递后的 rs2_data
  // ============================================================
  val fwd_ex_rs2 = io.id_ex_reg_wen && !io.id_ex_mem_ren && 
                   (io.id_ex_rd_addr =/= 0.U) && (io.id_ex_rd_addr === io.id_rs2_addr)
  
  io.rs2_data_fwd := MuxCase(io.id_rs2_data_raw, Seq(
    fwd_ex_rs2 -> io.id_ex_rd_data,
    (io.ex_mem_reg_wen && (io.ex_mem_rd_addr =/= 0.U) && (io.ex_mem_rd_addr === io.id_rs2_addr)) -> io.ex_mem_rd_data,
    (io.mem_wb_reg_wen && (io.mem_wb_rd_addr =/= 0.U) && (io.mem_wb_rd_addr === io.id_rs2_addr)) -> io.mem_wb_rd_data
  ))
}
