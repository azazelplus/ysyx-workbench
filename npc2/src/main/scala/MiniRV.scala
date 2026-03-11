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
  val gprfile = Module(new GPRFile)
  val csrfile = Module(new CSRFile)
  val regSync = Module(new RegFileSync)  // GPR+CSR 统一同步到 C++ 端 (Difftest)
  val pmem = Module(new PMEM)  // 统一的物理存储器模块

  // =================================================================================
  // 2. IF阶段连线
  // =================================================================================
  // 单周期：跳转信号来自 EXU 组合逻辑输出，直接反馈到 IFU
  ifu.io.jump_en   := exu.io.jump_en
  ifu.io.jump_addr := exu.io.jump_addr
  ifu.io.stall     := false.B  // 单周期无暂停

  // 连接 IFU 到 PMEM (指令存储器)
  pmem.io.imem <> ifu.io.imem

  // =================================================================================
  // 3. ID阶段连线
  // =================================================================================
  // 单周期：直接连接 IFU 输出到 IDU 输入（无 IF/ID 寄存器）
  idu.io.in <> ifu.io.out

  // 寄存器堆读取
  gprfile.io.rs1_addr := idu.io.rs1_addr
  gprfile.io.rs2_addr := idu.io.rs2_addr
  
  // 单周期：直接使用寄存器堆读出的值（无数据前递）
  idu.io.rs1_data := gprfile.io.rs1_data
  idu.io.rs2_data := gprfile.io.rs2_data

  // =================================================================================
  // 4. EX 阶段 (Execute)
  // =================================================================================
  // 单周期：直接连接 IDU 输出到 EXU 输入（无 ID/EX 寄存器）
  exu.io.in := idu.io.out
  
  // CSR 模块连接
  csrfile.io.csr_addr  := exu.io.csr_addr
  csrfile.io.csr_wen   := exu.io.csr_wen
  csrfile.io.csr_wdata := exu.io.csr_wdata
  exu.io.csr_rdata := csrfile.io.csr_rdata
  
  csrfile.io.exception_en := exu.io.is_ecall || exu.io.is_ebreak // 处理 ecall 和 ebreak 异常
  csrfile.io.cause        := Mux(exu.io.is_ebreak, 3.U, 11.U)     // Breakpoint (3) or M-mode ecall (11)
  csrfile.io.exception_pc := idu.io.out.pc
  
  csrfile.io.is_mret      := exu.io.is_mret
  
  exu.io.mtvec        := csrfile.io.mtvec_out
  exu.io.mepc         := csrfile.io.mepc_out

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
  
  gprfile.io.rd_addr := wbu.io.rd_addr
  gprfile.io.rd_data := wbu.io.rd_data
  gprfile.io.rd_wen  := wbu.io.rd_wen

  // =================================================================================
  // 7. 其他 (EBREAK 检测与调试)
  // =================================================================================
  // EBREAK 检测 (通过 PMEM 模块)
  pmem.io.ebreak_inst  := ifu.io.out.bits.inst
  pmem.io.ebreak_valid := true.B

  // 调试输出
  io.debug_pc   := ifu.io.out.bits.pc
  io.debug_inst := ifu.io.out.bits.inst

  // =================================================================================
  // 8. Difftest: 寄存器同步 (GPR + CSR -> C++)
  // =================================================================================
  regSync.io.clock         := clock
  regSync.io.gpr           := gprfile.io.gpr_sync
  regSync.io.csr_mstatus   := csrfile.io.csr_sync.mstatus
  regSync.io.csr_mtvec     := csrfile.io.csr_sync.mtvec
  regSync.io.csr_mepc      := csrfile.io.csr_sync.mepc
  regSync.io.csr_mcause    := csrfile.io.csr_sync.mcause
  regSync.io.csr_mcycle    := csrfile.io.csr_sync.mcycle
  regSync.io.csr_mcycleh   := csrfile.io.csr_sync.mcycleh
  regSync.io.csr_mvendorid := csrfile.io.csr_sync.mvendorid
  regSync.io.csr_marchid   := csrfile.io.csr_sync.marchid

  // =================================================================================
  // 9. 调试打印 (Debug Printfs)
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
