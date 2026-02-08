// MiniRV 执行单元 (Execution Unit)
package minirv.exu

import chisel3._
import chisel3.util._
import minirv._

/**
  * EXU - 执行单元
  * 
  * 功能：
  * 1. ALU 运算
  * 2. 分支/跳转地址计算
  * 3. 分支条件判断
  */
class EXU extends Module {
  val io = IO(new Bundle {
    // ========== IDU->EXU ==========
    val in = Input(new ID2EX)   // 输入: IDU->EXU, 指令控制/操作数/立即数/pc

    // ========== EXU->LSU ==========
    val out = Output(new EX2LS) // 输出: EXU->LSU, 访存地址/写数据/控制信号

    // ========== EXU->IFU (控制冒险重定向) ==========
    val jump_en   = Output(Bool())                    // EXU->IFU, 分支/跳转是否发生
    val jump_addr = Output(UInt(Config.ADDR_WIDTH.W)) // EXU->IFU, 跳转目标地址
    
    // ========== EXU<->CSRFile ==========
    val csr_rdata = Input(UInt(Config.XLEN.W))    // CSR 读数据
    val csr_wdata = Output(UInt(Config.XLEN.W))   // CSR 写数据
    val csr_addr  = Output(UInt(12.W))            // CSR 地址
    val csr_wen   = Output(Bool())                // CSR 写使能
    val is_ecall  = Output(Bool())                // ECALL 指令 
    val is_ebreak = Output(Bool())                // EBREAK 指令
    val is_mret   = Output(Bool())                // MRET 指令
    val mtvec     = Input(UInt(Config.XLEN.W))    // mtvec 值 (for `ecall` jump)
    val mepc      = Input(UInt(Config.XLEN.W))    // mepc 值 (for `mret` jump)
  })

  val in = io.in

  // ALU 操作数
  val alu_a = in.rs1_data
  val alu_b = Mux(in.alu_src, in.imm, in.rs2_data)

  // CSR ALU 子模块实例化.
  val csralu = Module(new CSRALU)
  csralu.io.csr_op    := in.csr_op
  csralu.io.rs1_data  := in.rs1_data
  csralu.io.imm       := in.imm
  csralu.io.csr_rdata := io.csr_rdata
  
  io.csr_wdata := csralu.io.csr_wdata
  
  io.csr_addr := in.csr_addr
  io.csr_wen  := in.is_csr
  io.is_ecall := in.is_ecall
  io.is_ebreak := in.is_ebreak
  io.is_mret  := in.is_mret

  // ALU 计算. .asSInt是as Signed Int. 搭配<<

  // ALU 计算. .asSInt是as Signed Int. 搭配<<
  val alu_result = WireDefault(0.U(Config.XLEN.W))
  
  switch(in.alu_op) {
    is(ALUOp.ADD)  { alu_result := alu_a + alu_b }
    is(ALUOp.SUB)  { alu_result := alu_a - alu_b }
    is(ALUOp.AND)  { alu_result := alu_a & alu_b }
    is(ALUOp.OR)   { alu_result := alu_a | alu_b }
    is(ALUOp.XOR)  { alu_result := alu_a ^ alu_b }

    is(ALUOp.SLL)  { alu_result := alu_a << alu_b(4, 0) }
    is(ALUOp.SRL)  { alu_result := alu_a >> alu_b(4, 0) }
    is(ALUOp.SRA)  { alu_result := (alu_a.asSInt >> alu_b(4, 0)).asUInt }

    is(ALUOp.SLT)  { alu_result := (alu_a.asSInt < alu_b.asSInt).asUInt }
    is(ALUOp.SLTU) { alu_result := (alu_a < alu_b).asUInt }
  }

  // Utype 的 LUI 和 AUIPC 特殊处理
  val lui_result   = in.imm                     // LUI: rd = imm << 12 (已在立即数生成时处理)
  val pc_plus_imm  = in.pc + in.imm             // AUIPC / JAL / Branch target


  // 根据这是啥指令(is_xxx信号)选择最终的 ALU 结果. 最终写回rd的值是final_alu_result.
  val final_alu_result = MuxCase(alu_result, Seq(
    in.is_lui   -> lui_result,    // 如果这是lui指令, in.is_lui===1, 选择lui_result存入rd.
    in.is_auipc -> pc_plus_imm,   // 如果这是auipc指令, in.is_auipc===1, 选择pc_plus_imm存入rd.
    (in.is_jal || in.is_jalr) -> (in.pc + 4.U), // 如果这是jal/jalr指令, 选择PC+4存入rd.
    in.is_csr   -> io.csr_rdata   // 如果这是csr指令, 将旧值写入 rd.
  ))



  // 处理B-Type. 分支条件判断 (根据 branch_op / funct3)
  val rs1_s = in.rs1_data.asSInt  // 有符号解释
  val rs2_s = in.rs2_data.asSInt
  // 控制信号branch_taken: 是否满足分支条件.
  val branch_taken = WireDefault(false.B)
  when(in.is_branch) {
    switch(in.branch_op) {
      is(BranchOp.BEQ)  { branch_taken := in.rs1_data === in.rs2_data }         // 相等
      is(BranchOp.BNE)  { branch_taken := in.rs1_data =/= in.rs2_data }         // 不相等
      is(BranchOp.BLT)  { branch_taken := rs1_s < rs2_s }                       // 有符号小于
      is(BranchOp.BGE)  { branch_taken := rs1_s >= rs2_s }                      // 有符号大于等于
      is(BranchOp.BLTU) { branch_taken := in.rs1_data < in.rs2_data }           // 无符号小于
      is(BranchOp.BGEU) { branch_taken := in.rs1_data >= in.rs2_data }          // 无符号大于等于
    }
  }



  // 跳转控制.
  // 如果是jal, jalr, branch&&branch_taken, ecall, ebreak, mret指令, 使能跳转信号jump_en
  io.jump_en := in.is_jal || in.is_jalr || (in.is_branch && branch_taken) || in.is_ecall || in.is_ebreak || in.is_mret
  // 跳转地址选择.
  io.jump_addr := MuxCase(pc_plus_imm, Seq(
    in.is_jalr  -> ((in.rs1_data + in.imm) & ~1.U(Config.ADDR_WIDTH.W)),  //jalr指令跳转到rs1+imm,并& ~1一下, 最低位清零对齐. 注意->优先级没&高, 右值加个括号.
    in.is_ecall -> io.mtvec,    //ecall指令跳转到mtvec寄存器指定的地址
    in.is_ebreak -> io.mtvec,   //ebreak指令跳转到mtvec寄存器指定的地址
    in.is_mret  -> io.mepc      //mret指令跳转到mepc寄存器指定的地址
  ))



  // 输出到 LSU
  io.out.alu_result := final_alu_result
  io.out.store_data := in.rs2_data  //
  io.out.rd_addr    := in.rd_addr   // rd_addr信号在exu透传.
  io.out.mem_wen    := in.mem_wen   // mem_wen信号在exu透传.
  io.out.mem_ren    := in.mem_ren   // mem_ren信号在exu透传.
  io.out.mem_op     := in.mem_op    // mem_op信号在exu透传. 传递内存操作类型
  io.out.reg_wen    := in.reg_wen   // reg_wen信号在exu透传.
}





/*
  // MuxCase 实现优先级编码的MUX. 签名: MuxCase(default: T, choices: Seq[(Bool, T)]): T
  // 第一个参数default是默认值(键值对的值), 第二个参数choices是选择列表(布尔条件, 输出值). 列表按优先级排列!
  // (a->b)等价于(a, b), 元组语法糖. 
*/





/**
和CSR的交互接口统统被放在EXU, 这种情况和GPR不同. 后者在IDU就实现了访问接口, 而在WBU实现写回.

* CSR指令大多是原子化不可分割的, 要求逻辑上一瞬间完成. 回忆一下, RISCV手册要求"精确例外": 保证异常之前的所有指令都完整地执行了，而
后续的指令都没有开始执行（或等同于没有执行） . 为了未来流水线等等复杂结构扩展, CSR的读写操作逻辑要内聚一下还是有必要的. 况且CSR不需要极致性能优化, 它们出现的频率很小. 事实上, 高性能处理器修改CSR通常是串行化Serializing的...
* 而GPR的读写就要求性能了. 尽快读(IDU就读), 流水线.
*/
