// package azazerv.core
// import chisel3._

// // =====================================================================
// //  Pipeline packet bundles. 定义所有流水线阶段之间传递的包结构.
// // =====================================================================

// /** Packet flowing from PC register to IF stage */
// class PC2IFPacket extends Bundle {
//   val kill = Bool()   //即inst_valid信号, 用于指示当前PC值无效, 需要被IF阶段丢弃
//   val pc = UInt(Parameters.AddrWidth)
//   val instId = UInt(64.W) // 全局指令id. 用于跟踪指令流, 生成新的instId时需要保证全局唯一. 
// }

// /** Packet flowing from IF stage to ID stage */
// class IF2IDPacket extends Bundle {
//   val kill = Bool() 
//   val pc = UInt(Parameters.AddrWidth)
//   val predictedTaken = Bool()   // 该指令被预测为是跳转.
//   val predictedTarget = UInt(Parameters.AddrWidth)  //预测的下一条指令地址
//   val instruction = UInt(Parameters.DataWidth)
//   val instId = UInt(64.W)
// }

// /** Packet flowing from ID stage to EX stage */
// class ID2EXPacket extends Bundle {
//   val kill = Bool()
//   val pc = UInt(Parameters.AddrWidth)
//   val predictedTaken = Bool()
//   val predictedTarget = UInt(Parameters.AddrWidth)
//   val rd = UInt(Parameters.PhysicalRegisterAddrWidth)
//   val rs1Addr = UInt(Parameters.PhysicalRegisterAddrWidth)
//   val rs2Addr = UInt(Parameters.PhysicalRegisterAddrWidth)
//   val rs1Data = UInt(Parameters.DataWidth)
//   val rs2Data = UInt(Parameters.DataWidth)
//   val imm = UInt(Parameters.DataWidth)
//   val uimm = UInt(Parameters.PhysicalRegisterAddrWidth) // 用于CSR指令的立即数, 代表CSR地址

//   /** false = use rs1, true = use PC as ALU op1 */
//   val aluOp1Pc = Bool() 

//   /** false = use rs2, true = use immediate as ALU op2 */
//   val aluOp2Imm = Bool()
//   val aluFunc = ALUFunctions()
//   val lsRead = Bool()
//   val lsWrite = Bool()
//   val funct3 = UInt(3.W)
//   val regWrite = Bool()
//   val regWSrc = UInt(2.W) // see RegWriteSource
//   val csrRead = Bool()
//   val csrWrite = Bool()
//   val csrAddr = UInt(Parameters.CSRRegisterAddrWidth)
//   val csrRData = UInt(Parameters.DataWidth) // CSR read value captured in ID
//   val isBranch = Bool()
//   val isJalr = Bool()
//   val isEcall = Bool()
//   val isMret = Bool()
//   val instId = UInt(64.W)
// }

// /** Packet flowing from EX stage to LS stage */
// class EX2LSPacket extends Bundle {
//   val kill = Bool()
//   val pc = UInt(Parameters.AddrWidth)
//   val rd = UInt(Parameters.PhysicalRegisterAddrWidth)
//   val aluResult = UInt(Parameters.DataWidth)
//   val rs2Data = UInt(Parameters.DataWidth)
//   val lsRead = Bool()
//   val lsWrite = Bool()
//   val funct3 = UInt(3.W)
//   val regWrite = Bool()
//   val regWSrc = UInt(2.W)
//   val csrWrite = Bool()
//   val csrAddr = UInt(Parameters.CSRRegisterAddrWidth)
//   val csrWData = UInt(Parameters.DataWidth)
//   val csrRData = UInt(Parameters.DataWidth)
//   val instId = UInt(64.W)
// }

// /** Packet flowing from LS stage to WB stage */
// class LS2WBPacket extends Bundle {
//   val kill = Bool()
//   val pc = UInt(Parameters.AddrWidth)
//   val rd = UInt(Parameters.PhysicalRegisterAddrWidth)
//   val aluResult = UInt(Parameters.DataWidth)
//   val lsRData = UInt(Parameters.DataWidth)
//   val regWrite = Bool()
//   val regWSrc = UInt(2.W)
//   val csrWrite = Bool()
//   val csrAddr = UInt(Parameters.CSRRegisterAddrWidth)
//   val csrWData = UInt(Parameters.DataWidth)
//   val csrRData = UInt(Parameters.DataWidth)
//   val instId = UInt(64.W)
// }



// // =====================================================================
// //  Debug info bundles (for simulation visibility)
// // =====================================================================
// class BasicStageDebugInfo extends Bundle {
//   val valid = Bool()
//   val stall = Bool()
//   val kill = Bool()
//   val instId = UInt(64.W)
// }

// class IFDebugInfo extends BasicStageDebugInfo

// class IDDebugInfo extends BasicStageDebugInfo {
//   val inst = UInt(Parameters.AddrWidth)
//   val pc = UInt(Parameters.AddrWidth)
// }

// class EXDebugInfo extends BasicStageDebugInfo

// class LSDebugInfo extends BasicStageDebugInfo

// class WBDebugInfo extends BasicStageDebugInfo

// class PerfDebugInfo extends Bundle {
//   val dataHazard = Bool()
//   val dataHazardEx = Bool()
//   val dataHazardLs = Bool()
//   val dataHazardRs1 = Bool()
//   val dataHazardRs2 = Bool()
//   val dataHazardExLoad = Bool()
//   val dataHazardLsLoad = Bool()
//   val systemHazard = Bool()

//   val ifWaitAr = Bool()
//   val ifWaitResp = Bool()

//   val lsWaitReadAddr = Bool()
//   val lsWaitReadData = Bool()
//   val lsWaitWriteAddr = Bool()
//   val lsWaitWriteData = Bool()
//   val lsWaitWriteResp = Bool()

//   val redirect = Bool()
//   val redirectWait = Bool()
//   val redirectFire = Bool()
//   val redirectIsBranch = Bool()
//   val redirectIsJump = Bool()
//   val redirectIsTrap = Bool()
//   val redirectIsMret = Bool()

//   val killedIf = Bool()
//   val killedId = Bool()
// }




// class CPUDebugInfo extends Bundle {
//   val ifStage = new IFDebugInfo
//   val idStage = new IDDebugInfo
//   val exStage = new EXDebugInfo
//   val lsStage = new LSDebugInfo
//   val wbStage = new WBDebugInfo
//   val perf = new PerfDebugInfo
// }



// class NextPcPredictInfo extends Bundle {
//   val valid = Bool()
//   val target = UInt(Parameters.AddrWidth)
// }

// class PredictorUpdateInfo extends Bundle {
//   val bhtValid = Bool()
//   val bhtPc = UInt(Parameters.AddrWidth)
//   val bhtTaken = Bool()
//   val btbValid = Bool()
//   val btbPc = UInt(Parameters.AddrWidth)
//   val btbTarget = UInt(Parameters.AddrWidth)
//   val rasPushValid = Bool()
//   val rasPushAddr = UInt(Parameters.AddrWidth)
//   val rasPopValid = Bool()
// }

// class GPRWritePort extends Bundle {
//   val enable = Bool()
//   val addr = UInt(Parameters.PhysicalRegisterAddrWidth)
//   val data = UInt(Parameters.DataWidth)
// }

// class GPRReadPort extends Bundle {
//   val addr = Input(UInt(Parameters.PhysicalRegisterAddrWidth))
//   val data = Output(UInt(Parameters.DataWidth))
// }

// class GPRReadPorts extends Bundle {
//   val rs1 = new GPRReadPort
//   val rs2 = new GPRReadPort
// }

// // GPR调试接口. 
// class GPRDebugPort extends Bundle {
//   val address = Input(UInt(Parameters.PhysicalRegisterAddrWidth))
//   val data = Output(UInt(Parameters.DataWidth))
// }

// class CSRWritePort extends Bundle {
//   val enable = Bool()
//   val addr = UInt(Parameters.CSRRegisterAddrWidth)
//   val data = UInt(Parameters.DataWidth)
// }

// class CSRReadPort extends Bundle {
//   val addr = Input(UInt(Parameters.CSRRegisterAddrWidth))
//   val data = Output(UInt(Parameters.DataWidth))
// }

// class CSRControlPort extends Bundle {
//   val trapValid = Bool()
//   val trapEpc = UInt(Parameters.DataWidth)
//   val trapCause = UInt(Parameters.DataWidth)
//   val mretValid = Bool()
// }

// class CSRStatusPort extends Bundle {
//   val interruptEnable = Bool()
//   val mtvec = UInt(Parameters.DataWidth)
//   val mepc = UInt(Parameters.DataWidth)
//   val mstatus = UInt(Parameters.DataWidth)
// }















