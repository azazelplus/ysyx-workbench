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
  })

  val in = io.in

  // ALU 操作数
  val alu_a = in.rs1_data
  val alu_b = Mux(in.alu_src, in.imm, in.rs2_data)

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


  // 选择最终的 ALU 结果. 
  // MuxCase 实现优先级编码的MUX. 签名: MuxCase(default: T, choices: Seq[(Bool, T)]): T
  // 第一个参数default是默认值(键值对的值), 第二个参数choices是选择列表(布尔条件, 输出值). 列表按优先级排列!
  // (a->b)等价于(a, b), 元组语法糖. 
  val final_alu_result = MuxCase(alu_result, Seq(
    in.is_lui   -> lui_result,    // lui优先级最高. 如果in.is_lui===1, 选择lui_result为最终输出.
    in.is_auipc -> pc_plus_imm,   // auipc次之.
    (in.is_jal || in.is_jalr) -> (in.pc + 4.U)  // JAL/JALR 存 PC+4.
    //如果不是lui, 
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


  // 跳转地址计算
  // val branch_addr = pc_plus_imm         // B-type, JAL (已合并到 pc_plus_imm)
  val jalr_addr   = (in.rs1_data + in.imm) & ~1.U(Config.ADDR_WIDTH.W)  // JALR. `& ~1`即强制最低位清零, 保证对齐.



  // 跳转控制
  io.jump_en := in.is_jal || in.is_jalr || (in.is_branch && branch_taken) //la
  io.jump_addr := Mux(in.is_jalr, jalr_addr, pc_plus_imm)



  // 输出到 LSU
  io.out.alu_result := final_alu_result
  io.out.store_data := in.rs2_data  //
  io.out.rd_addr    := in.rd_addr   // rd_addr信号在exu透传.
  io.out.mem_wen    := in.mem_wen   // mem_wen信号在exu透传.
  io.out.mem_ren    := in.mem_ren   // mem_ren信号在exu透传.
  io.out.mem_op     := in.mem_op    // mem_op信号在exu透传. 传递内存操作类型
  io.out.reg_wen    := in.reg_wen   // reg_wen信号在exu透传.
}
