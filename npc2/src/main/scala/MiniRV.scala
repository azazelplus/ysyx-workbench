// MiniRV 顶层模块 - 单周期版本
// 简化自五级流水线，移除了流水线寄存器、HDU、FWU

package minirv

import chisel3._
import chisel3.util._
import minirv.ifu._
import minirv.idu._
import minirv.exu._
import minirv.lsu._
import minirv.wbu._
import _root_.circt.stage.ChiselStage


/**
  * MiniRV 顶层模块 - 单周期实现
  * 
  * 数据通路：IF -> ID -> EX -> MEM(LSU) -> WB
  * 所有阶段在同一时钟周期内完成
  * 
  * 特点：
  *   - 无流水线寄存器
  *   - 无数据前递（FWU）- 单周期不存在数据冒险
  *   - 无暂停逻辑（HDU）- 单周期不存在结构冒险
  * 
  * 使用 DPI-C 机制访问存储器
  */
class MiniRV extends Module {
  val io = IO(new Bundle {
    // 调试接口
    val debug_pc = Output(UInt(Config.ADDR_WIDTH.W))
    val debug_inst = Output(UInt(Config.INST_WIDTH.W))
  })

  // =================================================================================
  // 1. 实例化各功能模块
  // =================================================================================
  val ifu = Module(new IFU)
  val idu = Module(new IDU)
  val exu = Module(new EXU)
  val lsu = Module(new LSU)
  val wbu = Module(new WBU)
  val regfile = Module(new RegFile)
  val pmem = Module(new PMEM)  // 统一的物理存储器模块

  // =================================================================================
  // 2. IF 阶段 (Instruction Fetch)
  // =================================================================================
  // 单周期：跳转信号来自 EXU 组合逻辑输出，直接反馈到 IFU
  ifu.io.jump_en   := exu.io.jump_en
  ifu.io.jump_addr := exu.io.jump_addr
  ifu.io.stall     := false.B  // 单周期无暂停

  // 连接 IFU 到 PMEM (指令存储器)
  pmem.io.imem_addr := ifu.io.imem_addr
  ifu.io.imem_rdata := pmem.io.imem_rdata

  // =================================================================================
  // 3. ID 阶段 (Instruction Decode)
  // =================================================================================
  // 单周期：直接连接 IFU 输出到 IDU 输入（无 IF/ID 寄存器）
  idu.io.in.pc   := ifu.io.out.pc
  idu.io.in.inst := ifu.io.out.inst

  // 寄存器堆读取
  regfile.io.rs1_addr := idu.io.rs1_addr
  regfile.io.rs2_addr := idu.io.rs2_addr
  
  // 单周期：直接使用寄存器堆读出的值（无数据前递）
  idu.io.rs1_data := regfile.io.rs1_data
  idu.io.rs2_data := regfile.io.rs2_data

  // =================================================================================
  // 4. EX 阶段 (Execute)
  // =================================================================================
  // 单周期：直接连接 IDU 输出到 EXU 输入（无 ID/EX 寄存器）
  exu.io.in := idu.io.out

  // =================================================================================
  // 5. MEM 阶段 (Memory Access)
  // =================================================================================
  // 单周期：直接连接 EXU 输出到 LSU 输入（无 EX/MEM 寄存器）
  lsu.io.in := exu.io.out

  // 连接 LSU 到 PMEM (数据存储器)
  lsu.io.dmem <> pmem.io.dmem

  // =================================================================================
  // 6. WB 阶段 (Write Back)
  // =================================================================================
  // 单周期：直接连接 LSU 输出到 WBU 输入（无 MEM/WB 寄存器）
  wbu.io.in := lsu.io.out
  
  regfile.io.rd_addr := wbu.io.rd_addr
  regfile.io.rd_data := wbu.io.rd_data
  regfile.io.rd_wen  := wbu.io.rd_wen

  // =================================================================================
  // 7. 其他 (EBREAK 检测与调试)
  // =================================================================================
  // EBREAK 检测 (通过 PMEM 模块)
  pmem.io.ebreak_inst  := ifu.io.out.inst
  pmem.io.ebreak_valid := true.B

  // 调试输出
  io.debug_pc   := ifu.io.out.pc
  io.debug_inst := ifu.io.out.inst

  // =================================================================================
  // 8. 调试打印 (Debug Printfs)
  // =================================================================================
  // 打印 WB 阶段写寄存器
  when(wbu.io.rd_wen && (wbu.io.rd_addr =/= 0.U)) {
    printf("[DEBUG] WB: Reg[%d] <= 0x%x\n", wbu.io.rd_addr, wbu.io.rd_data)
  }
}


/**
  * 生成 Verilog
  * 运行命令：./mill npc2.runMain minirv.MiniRV
  */
object MiniRV extends App {
  // 指定输出目录
  val outputDir = "generated"
  
  println(s"正在生成所有.sv 文件到 $outputDir/ ...")
  ChiselStage.emitSystemVerilogFile(
    new MiniRV,
    args = Array("--target-dir", outputDir),
    firtoolOpts = Array("-disable-all-randomization", "-strip-debug-info")
  )
  println(s"生成完成！文件位置: $outputDir/MiniRV.sv")
}
