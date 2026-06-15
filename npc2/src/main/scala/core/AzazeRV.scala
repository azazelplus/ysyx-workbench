// AzazeRV 顶层模块 - 多周期握手版本
//
// ┌─────────────────────────────── 整体架构 ──────────────────────────────────┐
// │                                                                          │
// │  指令生命周期: 每条指令依次经过 IFU → IDU → EXU → LSU → WBU 五个阶段.    │
// │  阶段之间通过 Decoupled (valid/ready) 握手 + PipeReg/Queue 缓冲连接.     │
// │                                                                          │
// │  ┌─────┐ PipeReg ┌─────┐ PipeReg ┌─────┐   Q(1)  ┌─────┐  Q(1)  ┌────┐│
// │  │ IFU ├──→ ▪ ──→│ IDU ├──→ ▪ ──→│ EXU ├──→ ▪ ──→│ LSU ├──→▪──→│WBU ││
// │  └──┬──┘  if_id  └─┬───┘  id_ex  └──┬──┘  ex_ls  └──┬──┘ ls_wb └─┬──┘│
// │     │               │                │                │             │    │
// │  ┌──┴──┐         ┌──┴──┐         ┌───┴──┐         ┌──┴──┐      ┌──┴──┐│
// │  │PMEM │         │ GPR │         │ CSR  │         │PMEM │      │ GPR ││
// │  │IMEM │         │read │         │ File │         │DMEM │      │write││
// │  └─────┘         └─────┘         └──────┘         └─────┘      └─────┘│
// │                                                                        │
// │  关键设计决策:                                                          │
// │                                                                        │
// │  1. 一次一条 (pipeline_busy):                                           │
// │     同一时刻只有一条指令在 IDU→WBU 路径中.                               │
// │     这消除了所有 RAW 数据冒险, 无需数据前递 (forwarding).                │
// │     IFU 可以预取下一条指令到 if_id_q, 但 IDU 被门控,                    │
// │     直到 WBU 提交后才放行. 此时 GPR 已有最新值.                          │
// │                                                                        │
// │  2. 跳转冲刷 (flush + inst_valid):                                      │
// │     当 EXU 检测到 jal/jalr/branch/ecall/ebreak/mret 时:                 │
// │       - if_id_q 和 id_ex_q (PipeReg) 中的指令被标记 inst_valid=false    │
// │       - IFU 丢弃当前 PMEM 响应, 用新 PC 重新取指                        │
// │       - 下游模块检测 inst_valid=false, 透传而不执行                     │
// │       - 跳转指令本身继续流过 LSU→WBU 完成写回 (如 jal 写 ra)            │
// │       - pipeline_busy 不在 flush 时清除, 而是等 WBU 自然提交             │
// │         (否则 IDU 可能在 jal 写回 ra 之前就读取了旧的 ra 值)             │
// │                                                                        │
// └────────────────────────────────────────────────────────────────────────┘

package azazerv

import chisel3._
import chisel3.util._
import azazerv.ifu._
import azazerv.idu._
import azazerv.exu._
import azazerv.lsu._
import azazerv.wbu._
import _root_.circt.stage.ChiselStage


/**
  * AzazeRV 顶层模块 - 多周期握手实现
  *
  * 数据通路：IFU -> PipeReg -> IDU -> PipeReg -> EXU -> Queue -> LSU -> Queue -> WBU
  *   - if_id_q, id_ex_q 使用 PipeReg (支持 inst_valid 冲刷语义)
  *   - ex_ls_q, ls_wb_q 使用 Queue (无需冲刷，inst_valid 自然透传)
  *
  * 特点：
  *   - IFU, PMEM 使用 AXI-like 接口 (AR/R 取指, AR/R + AW/W/B 访存)
  *   - 无数据前递 (FWU) - 暂无
  *   - 无暂停逻辑 (HDU) - 暂无
  */
class AzazeRV extends Module {
  val io = IO(new Bundle {
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
  val regSync = Module(new RegFileSync)
  val pmem = Module(new PMEM)
  val arbiter = Module(new AXILiteArbiter)  // AXI-Lite 总线仲裁器


  // =================================================================================
  // 2. IF阶段连线
  // =================================================================================
  // 跳转信号来自 EXU
  ifu.io.jump_en   := exu.io.jump_en
  ifu.io.jump_addr := exu.io.jump_addr

  // IFU <-> Arbiter <-> PMEM (通过 AXI-Lite 总线)
  arbiter.io.master0 <> ifu.io.axi   // IFU 为 Master 0 (优先级低)
  arbiter.io.master1 <> lsu.io.axi   // LSU 为 Master 1 (优先级高)
  pmem.io.axi <> arbiter.io.slave    // PMEM 为 Slave

  // =================================================================================
  // 3. 流水线互锁 + 跳转冲刷
  //
  // ── 问题1: 数据冒险 (RAW Hazard) ──────────────────────────────────────────
  //
  //    Queue(1) 允许指令重叠: 当 inst1 还在 EXU/LSU/WBU 时, inst2 已经进入 IDU.
  //    如果 inst1 写寄存器 x, inst2 读寄存器 x, 那 inst2 读到的是旧值. 例如:
  //
  //      auipc sp, 9       ← 写 sp         (还在 LSU/WBU, 没写回)
  //      addi  sp, sp, -4  ← 读 sp = 0!    (IDU 此时读 GPR, sp 还是旧值)
  //
  //    解决方案: pipeline_busy 标志.
  //      - IDU 发出指令时 → busy = true (阻止下一条进入 IDU)
  //      - WBU 提交指令时 → busy = false (GPR 已更新, 下一条可安全读取)
  //
  //    时序保证:
  //      Cycle N:   WBU 提交, GPR.write 生效 (时钟沿), busy 寄存器写 false
  //      Cycle N+1: busy = false, GPR 已有新值, IDU 读到正确数据 ✓
  //
  // ── 问题2: 控制冒险 (跳转后取错指令) ─────────────────────────────────────
  //
  //    IFU 不知道当前指令是否跳转, 总是乐观地预取 PC+4. 当 EXU 发现跳转时,
  //    if_id_q 和 id_ex_q 中可能已经有了错误指令, 必须冲刷掉.
  //
  //    解决方案: flush = exu.io.jump_en
  //      - if_id_q 和 id_ex_q (PipeReg) 中的指令被标记 inst_valid=false
  //      - IFU 丢弃当前 PMEM 响应, 用 jump_addr 重新取指
  //      - 下游模块检测 inst_valid=false, 透传而不执行
  //      - pipeline_busy 不在 flush 时清除: 跳转指令 (如 jal) 可能要写 ra,
  //        必须等它走完 LSU→WBU 提交后, GPR 才有正确的 ra 值.
  //
  //    一条 jal 指令的完整时序:
  //      Cycle 0: EXU 处理 jal, jump_en=1, flush 冲刷 Queue
  //      Cycle 1: jal 进入 ex_ls_q → LSU (非访存, 直接 s_done)
  //      Cycle 2: LSU → ls_wb_q → WBU 写回 ra = PC+4, busy = false
  //      Cycle 3: IFU 取到跳转目标指令, IDU 拿到 (busy 已清), 读到正确的 ra ✓
  //
  // =================================================================================
  val pipeline_busy = RegInit(false.B)

  // 跳转冲刷信号
  val flush = exu.io.jump_en

  // IFU -> PipeReg(2) -> IDU   (跳转时标记 inst_valid=false)
  // PipeReg 支持 inst_valid 冲刷语义，不清空队列，而是标记无效
  // flushAffectsOutput=false: 避免组合环路 (flush 来自 EXU，不能影响当前输出)
  val if_id_q = PipeReg(ifu.io.out, flush, entries = 2, flushAffectsOutput = false)

  // 手动连接 if_id_q → IDU, 用 pipeline_busy 门控:
  //   pipeline_busy 时, IDU 不接受新指令, if_id_q 保持数据不弹出.
  idu.io.in.valid := if_id_q.valid && !pipeline_busy
  idu.io.in.bits  := if_id_q.bits
  if_id_q.ready   := idu.io.in.ready && !pipeline_busy

  // IDU -> PipeReg(1) -> EXU   (跳转时标记 inst_valid=false)
  // flushAffectsOutput=false: 避免组合环路 (EXU 的 jump_en 依赖 inst_valid)
  // 跳转指令 I_n 的 inst_valid 保持 true，后续指令从 invalidMask 获取 false
  val id_ex_q = PipeReg(idu.io.out, flush, entries = 1, flushAffectsOutput = false)
  exu.io.in <> id_ex_q

  // EXU -> Queue(1) -> LSU
  val ex_ls_q = Queue(exu.io.out, 1)
  lsu.io.in <> ex_ls_q

  // LSU -> Queue(1) -> WBU
  val ls_wb_q = Queue(lsu.io.out, 1)
  wbu.io.in <> ls_wb_q

  // pipeline_busy 控制:
  //   设置: IDU 将 **有效** 指令送入 id_ex_q (idu.io.out.fire && inst_valid)
  //   清除: WBU 提交指令 (wbu.io.inst_valid)
  //   注意: 无效指令 (flush 后被标记的) 不设置 pipeline_busy, 因为它们会被 id_ex_q 丢弃
  //         如果设置了, 会导致死锁 (等待永远不会到来的 WBU 提交)
  when(idu.io.out.fire && idu.io.out.bits.inst_valid) {
    pipeline_busy := true.B
  }.elsewhen(wbu.io.inst_valid) {
    pipeline_busy := false.B
  }

  // =================================================================================
  // 4. 寄存器堆连线
  // =================================================================================
  // IDU 读 GPR
  gprfile.io.rs1_addr := idu.io.rs1_addr
  gprfile.io.rs2_addr := idu.io.rs2_addr
  idu.io.rs1_data := gprfile.io.rs1_data
  idu.io.rs2_data := gprfile.io.rs2_data

  // WBU 写 GPR
  gprfile.io.rd_addr := wbu.io.rd_addr
  gprfile.io.rd_data := wbu.io.rd_data
  gprfile.io.rd_wen  := wbu.io.rd_wen

  // =================================================================================
  // 5. CSR 连线 (EXU <-> CSRFile)
  // =================================================================================
  csrfile.io.csr_addr  := exu.io.csr_addr
  csrfile.io.csr_wen   := exu.io.csr_wen
  csrfile.io.csr_wdata := exu.io.csr_wdata
  exu.io.csr_rdata := csrfile.io.csr_rdata

  csrfile.io.exception_en := exu.io.is_ecall || exu.io.is_ebreak
  csrfile.io.cause        := Mux(exu.io.is_ebreak, 3.U, 11.U)
  // exception_pc: EXU 当前正在处理的指令 PC
  csrfile.io.exception_pc := exu.io.in.bits.pc

  csrfile.io.is_mret := exu.io.is_mret

  exu.io.mtvec := csrfile.io.mtvec_out
  exu.io.mepc  := csrfile.io.mepc_out

  // =================================================================================
  // 6. EBREAK 检测与调试 (使用 WBU 输出)
  // =================================================================================
  pmem.io.ebreak_inst  := wbu.io.debug_inst
  pmem.io.ebreak_valid := wbu.io.inst_valid

  io.debug_pc   := wbu.io.debug_pc
  io.debug_inst := wbu.io.debug_inst

  // =================================================================================
  // 8. 寄存器同步 (GPR + CSR → C++, 通过标准 DPI-C 接口)
  // =================================================================================
  regSync.clock         := clock
  regSync.gpr           := gprfile.io.gpr_sync
  regSync.csr_mstatus   := csrfile.io.csr_sync.mstatus
  regSync.csr_mtvec     := csrfile.io.csr_sync.mtvec
  regSync.csr_mepc      := csrfile.io.csr_sync.mepc
  regSync.csr_mcause    := csrfile.io.csr_sync.mcause
  regSync.csr_mcycle    := csrfile.io.csr_sync.mcycle
  regSync.csr_mcycleh   := csrfile.io.csr_sync.mcycleh
  regSync.csr_mvendorid := csrfile.io.csr_sync.mvendorid
  regSync.csr_marchid   := csrfile.io.csr_sync.marchid

  // =================================================================================
  // 9. 调试打印
  // =================================================================================
  when(wbu.io.rd_wen && (wbu.io.rd_addr =/= 0.U)) {
    printf("[DEBUG] WB: Reg[%d] <= 0x%x\n", wbu.io.rd_addr, wbu.io.rd_data)
  }
}


/**
  * 生成 Verilog
  * 运行命令：./mill npc2.runMain azazerv.AzazeRV
  */
object AzazeRV extends App {
  val outputDir = "generated"

  println(s"正在生成所有.sv 文件到 $outputDir/ ...")
  ChiselStage.emitSystemVerilogFile(
    new AzazeRV,
    args = Array("--target-dir", outputDir),
    firtoolOpts = Array("-disable-all-randomization", "-strip-debug-info")
  )
  println(s"生成完成！文件位置: $outputDir/AzazeRV.sv")
}
