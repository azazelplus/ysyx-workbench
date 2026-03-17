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
  * 2. 通过 AXI AR/R 接口从 PMEM 读取指令 (1-cycle SRAM)
  * 3. 将 (pc, inst) 通过 Decoupled 接口传递给下游 if_id_q → IDU
  *
  * 状态机:
  *
  *    ┌──────────┐  ar.fire &&   ┌──────────┐
  *    │          │  !jump_en     │          │
  *    │  s_idle  ├──────────────→│  s_wait  │
  *    │          │               │          │
  *    └──┬───┬───┘               └──┬───┬───┘
  *       │   │                      │   │
  *       │   │ar.fire &&            │   │ !jump_en &&
  *       │   │jump_en               │   │ r.fire && out.fire
  *       │   │                      │   │ (正常投递给下游)
  *       │   ▼                      │   │
  *    ┌──┴──────┐                   │   │
  *    │         │←──────────────────┘   │
  *    │ s_flush │  jump_en &&          │
  *    │         │  !r.valid            │
  *    └────┬────┘                      │
  *         │ r.fire (丢弃)            │
  *         └──────────→ s_idle ←───────┘
  *
  * 关于跳转处理 (控制冒险):
  *   当 EXU 发出 jump_en 时, IFU 可能处于三种状态:
  *
  *   1. s_idle + ar 没发出: 最简单, 留在 s_idle, 下周期 PC 已更新, 正常取指.
  *
  *   2. s_idle + ar.fire 同周期: AR 已发给 PMEM (用的旧 PC!), PMEM 会返回
  *      错误指令. 必须进 s_flush 等待并丢弃该响应.
  *
  *   3. s_wait: PMEM 正在处理旧请求.
  *      - 如果 R 已经 valid: 立即接受并丢弃, 回到 s_idle.
  *      - 如果 R 还没 valid: 进入 s_flush 等待.
  */


class IFU extends Module {
  val io = IO(new Bundle {
    // ========== IFU->IDU ==========
    val out = Decoupled(new IF2ID)           // 输出: IFU->IDU, 传递取到的(pc, inst)

    // ========== EXU->IFU (控制冒险重定向) ==========
    val jump_en   = Input(Bool())                     // EXU->IFU, 是否跳转/分支成立
    val jump_addr = Input(UInt(Config.ADDR_WIDTH.W))  // EXU->IFU, 跳转目标地址

    // ========== IFU<->PMEM (取指接口) ==========
    val imem = new IMemIO                 // IFU 为 Master (发送 req, 接收 resp)
  })

  // 实例化 PC 模块
  val pc = Module(new PC)
  
  // 连接 PC 模块的控制信号
  pc.io.jump_en   := io.jump_en
  pc.io.jump_addr := io.jump_addr
  // =============================================================
  // IFU 状态机
  // s_idle:  发送 AR 地址请求
  // s_wait:  等待 R 返回并向下游(IF/ID)发送指令
  // s_flush: 跳转时丢弃 PMEM 正在返回的过期指令
  // =============================================================
  val s_idle :: s_wait :: s_flush :: Nil = Enum(3)
  val state = RegInit(s_idle)

  // 锁存本次取指对应的 PC，用于与 R.data 组装 IF2ID
  val reqPcReg = Reg(UInt(Config.ADDR_WIDTH.W))

  // Default
  io.imem.ar.valid := false.B
  io.imem.ar.bits.addr := pc.io.pc
  io.imem.r.ready := false.B

  io.out.valid := false.B
  io.out.bits.pc := reqPcReg
  io.out.bits.inst := io.imem.r.bits.data

  // 只有一条指令真正被 IF/ID 接收后，PC 才推进 (顺序 +4)
  pc.io.pc_en := io.out.fire

  switch(state) {
    is(s_idle) {
      io.imem.ar.valid := true.B
      io.imem.ar.bits.addr := pc.io.pc

      when(io.jump_en) {
        // 跳转发生在 s_idle: PC 已经被 PC 模块更新到 jump_addr.
        // 如果本周期同时 ar.fire 了, 那发出的地址是旧PC, 需要丢弃即将到来的 R 响应.
        when(io.imem.ar.fire) {
          state := s_flush
        }
        // 否则 AR 没发出, 留在 s_idle, 下周期用新 PC 重发即可.
      }.elsewhen(io.imem.ar.fire) {
        reqPcReg := pc.io.pc
        state := s_wait
      }
    }

    is(s_wait) {
      // 如果跳转发生, 丢弃当前正在等待的 PMEM 响应.
      when(io.jump_en) {
        // 尝试立即消耗 R 响应 (如果 PMEM 已经返回了)
        io.imem.r.ready := true.B
        io.out.valid := false.B
        when(io.imem.r.valid) {
          // PMEM 已经返回, 直接丢弃, 回到 s_idle 重新取指
          state := s_idle
        }.otherwise {
          // PMEM 还没返回, 进入 s_flush 等待并丢弃它
          state := s_flush
        }
      }.otherwise {
        io.out.valid := io.imem.r.valid
        io.out.bits.pc := reqPcReg
        io.out.bits.inst := io.imem.r.bits.data
        io.imem.r.ready := io.out.ready

        when(io.imem.r.fire) {
          state := s_idle
        }
      }
    }

    is(s_flush) {
      // 等待 PMEM R 通道返回过期数据, 接受但丢弃, 然后回到 s_idle
      io.imem.r.ready := true.B
      io.out.valid := false.B
      when(io.imem.r.fire) {
        state := s_idle
      }
    }
  }

}
// 关于Decoupled:
//  Decoupled 本身已经自带了方向属性, 它是一个专门为“生产者(Source)”设计的模板. 不需要写 Input/Output.
// 当你写 val out = Decoupled(new IF2ID) 时, Chisel 的 Decoupled 模板会自动为你在这个接口里创建3个信号, 并且预设了它们的相对方向:
  // bits: 类型是 IF2ID, 方向是 Output (要把取到的指令传出去).
  // valid: 类型是 Bool, 方向是 Output (告诉对方指令有效).
  // ready: 类型是 Bool, 方向是 Input (接收对方的反压信号).
    // fire: 内置逻辑, fire := valid && ready, 代表一次成功的握手 (数据传递).
    // 

// 关于Nil, Enum列表, `::`连接符:
// Nil 是 Scala 中的空列表. `::`连接列表的两个元素. Enum(x)返回值等价于List(0.U, 1.U, ..., x-1.U). 
// 语句val s_idle :: s_wait :: Nil = Enum(2) 等价于: 
  // val s_idle = 0.U
  // val s_wait = 1.U

// List, Nil, Vector, ArraySeq是Seq的子类型.
