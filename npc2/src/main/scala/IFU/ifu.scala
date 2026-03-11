// MiniRV 取指单元 (Instruction Fetch Unit)
package minirv.ifu

import chisel3._
import chisel3.util._
import minirv._

/**
  * IFU - 取指单元
  * 
  * 功能：
  * 1. 使用 PC 模块获取当前指令地址
  * 2. 通过外部存储器接口读取指令
  * 3. 将 PC 和指令传递给 IDU
  */


class IFU extends Module {
  val io = IO(new Bundle {
    // ========== IFU->IDU ==========
    val out = Decoupled(new IF2ID)           // 输出: IFU->IDU, 传递取到的(pc, inst)

    // ========== EXU->IFU (控制冒险重定向) ==========
    val jump_en   = Input(Bool())                     // EXU->IFU, 是否跳转/分支成立
    val jump_addr = Input(UInt(Config.ADDR_WIDTH.W))  // EXU->IFU, 跳转目标地址

    // ========== MiniRV(流水线控制)->IFU ==========
    val stall     = Input(Bool())       // 暂停信号. MiniRV->IFU, Load-Use 冒险时冻结 PC/IF

    // ========== IFU<->PMEM (取指接口) ==========
    val imem = new IMemIO                 // IFU 为 Master (发送 req, 接收 resp)
  })

  // 实例化 PC 模块
  val pc = Module(new PC)
  
  // 连接 PC 模块的控制信号
  pc.io.jump_en   := io.jump_en
  pc.io.jump_addr := io.jump_addr
  pc.io.stall     := io.stall

  // 输出指令地址到外部存储器
  io.imem.req.addr := pc.io.pc

  /***************************和IDU的握手逻辑*****************************/
  // ifu->idu状态机的状态是{fetch_done, ready}. 输出valid.
  // idu->ifu状态机的状态是{decode_done, valid}, 输出ready
  // 取指完成信号: 单周期=组合逻辑立即完成(true.B)
  // 未来接 AXI 总线时，替换为 io.imem.resp.valid
  val fetch_done = true.B

  val s_idle :: s_wait_ready :: Nil = Enum(2)
  val state = RegInit(s_idle)
  val skid_buffer = Reg(new IF2ID)  // 锁存取指结果, 保证 s_wait_ready 期间 bits 稳定

  switch(state) {
    is(s_idle) {
      when(fetch_done) {                        // 取指完成 → 锁存数据, 拉高 valid
        //从滑动缓冲区输出到idu
        skid_buffer.pc   := pc.io.pc
        skid_buffer.inst := io.imem.resp.data
        state := s_wait_ready
      }
    }
    is(s_wait_ready) {
      when(io.out.ready) {                      // IDU 握手消费完成 → 回到取指
        state := s_idle
      }
    }
  }

  // valid 由状态驱动(输出), 不参与状态跳转条件(避免组合环)
  io.out.valid := (state === s_wait_ready)
  // bits 从寄存器输出, 保证 s_wait_ready 期间数据稳定不受 PC 变化影响
  io.out.bits  := skid_buffer

}
// 关于Decoupled:
//  Decoupled 本身已经自带了方向属性, 它是一个专门为“生产者(Source)”设计的模板. 不需要写 Input/Output.
// 当你写 val out = Decoupled(new IF2ID) 时, Chisel 的 Decoupled 模板会自动为你在这个接口里创建3个信号, 并且预设了它们的相对方向:
  // bits: 类型是 IF2ID, 方向是 Output (要把取到的指令传出去).
  // valid: 类型是 Bool, 方向是 Output (告诉对方指令有效).
  // ready: 类型是 Bool, 方向是 Input (接收对方的反压信号).

// 关于Nil, Enum列表, `::`连接符:
// Nil 是 Scala 中的空列表. `::`连接列表的两个元素. Enum(x)返回值等价于List(0.U, 1.U, ..., x-1.U). 
// 语句val s_idle :: s_wait_ready :: Nil = Enum(2) 等价于: 
  // val s_idle = 0.U
  // val s_wait_ready = 1.U

