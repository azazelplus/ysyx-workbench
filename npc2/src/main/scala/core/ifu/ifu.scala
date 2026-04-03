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
  * 3. 将 (pc, inst, inst_valid) 通过 Decoupled 接口传递给下游 Queue → IDU
  *
  * 状态机 (简化版, 依赖 Queue(2) 缓冲):
  *
  *    ┌──────────┐  ar.fire &&   ┌──────────┐  out.fire   ┌──────────┐
  *    │          │  !jump_en     │          │ && !jump_en │          │
  *    │  s_idle  ├──────────────→│  s_wait  ├────────────→│  s_idle  │
  *    │          │               │          │             │          │
  *    └──┬───┬───┘               └──┬───────┘             └──────────┘
  *       │   │                      │
  *       │   │ar.fire &&            │ jump_en &&
  *       │   │jump_en               │ !r.valid
  *       │   │                      │
  *       │   ▼                      ▼
  *    ┌──┴──────┐              ┌──────────┐
  *    │         │←─────────────┤          │
  *    │ s_flush │              │  s_idle  │
  *    │         │──r.fire─────→│          │
  *    └─────────┘              └──────────┘
  *
  * 关键设计：
  * - s_idle 只在 out.ready 时发起 AR 请求，保证 r.fire 时 Queue 有空间
  * - s_wait 的 r.ready 跟随 out.ready，jump 时强制 r.ready=true 丢弃过期数据
  */


class IFU extends Module {
  val io = IO(new Bundle {
    // ========== IFU->IDU ==========
    val out = Decoupled(new IF2ID)           // 输出: IFU->IDU, 传递取到的(pc, inst)

    // ========== EXU->IFU (控制冒险重定向) ==========
    val jump_en   = Input(Bool())                     // EXU->IFU, 是否跳转/分支成立
    val jump_addr = Input(UInt(Config.ADDR_WIDTH.W))  // EXU->IFU, 跳转目标地址

    // ========== IFU<->PMEM (取指接口, AXI-Lite) ==========
    val axi = new AXILiteMasterIO         // IFU 为 AXI-Lite Master (只使用 AR/R 通道)
  })

  // 实例化 PC 模块
  val pc = Module(new PC)

  // 连接 PC 模块的控制信号
  pc.io.jump_en   := io.jump_en
  pc.io.jump_addr := io.jump_addr
  // =============================================================
  // IFU 状态机 (简化版，依赖 Queue(2) 缓冲)
  // s_idle:   发送 AR 地址请求
  // s_wait:   等待 R 返回，直接输出到下游 Queue
  // s_flush:  跳转时丢弃 PMEM 正在返回的过期指令
  // =============================================================
  val s_idle :: s_wait :: s_flush :: Nil = Enum(3)
  val state = RegInit(s_idle)

  // 锁存本次取指对应的 PC，用于与 R.data 组装 IF2ID
  val reqPcReg = Reg(UInt(Config.ADDR_WIDTH.W))

  // Default
  io.axi.ar.valid := false.B
  io.axi.ar.bits.addr := pc.io.pc
  io.axi.r.ready := false.B

  // Tie-off 写通道 (IFU 只读，永不写)
  io.axi.aw.valid := false.B
  io.axi.aw.bits.addr := 0.U
  io.axi.w.valid := false.B
  io.axi.w.bits.data := 0.U
  io.axi.w.bits.strb := 0.U
  io.axi.b.ready := false.B

  io.out.valid := false.B
  io.out.bits.pc := reqPcReg
  io.out.bits.inst := io.axi.r.bits.data
  io.out.bits.inst_valid := true.B  // 默认有效，flush 时置 false

  // 只有一条指令真正被 IF/ID 接收后，PC 才推进 (顺序 +4)
  pc.io.pc_en := io.out.fire



// 状态机
  switch(state) {

    // idle状态: 等待AR握手.
    is(s_idle) {
      // 只有当下游 Queue 有空间时才发起 AR 请求
      // 这保证了当 r.fire 发生时，Queue 一定有空间接收
      io.axi.ar.valid := io.out.ready

      // idle状态下的转移逻辑:
      // jump是优先级最高的覆盖信号.
      when(io.jump_en) {
        // 跳转发生在 s_idle: PC 已经被 PC 模块更新到 jump_addr.
        // 如果本周期同时 ar.fire 了, 那下周期将要fire的地址是旧PC, 需要丢弃即将到来的 R 响应.
        when(io.axi.ar.fire) {
          state := s_flush  //下个周期模式切换为flush
        }
        // 若!ar.fire, 下周期还不会ar通道fire, 留在 s_idle, 下周期PC自动更新, 故ifu不需要做什麽.
      }
      // 无跳转时: ar.fire时, 下个周期将发出ar请求. reqPcReg快照存储pc, 然后进入s_wait等待R响应.
      .elsewhen(io.axi.ar.fire) {
        reqPcReg := pc.io.pc
        state := s_wait
      }
    }

    // wait状态, 即ifu成功AR握手, 等待PMEM回复, 直接输出到下游Queue.
    is(s_wait) {
      // r.ready 跟随 out.ready (Queue 有空间时接收)
      // 依赖 Queue(2) 缓冲和 pipeline_busy 保证 Queue 有空间
      io.axi.r.ready := io.out.ready

      // wait状态下如果发生跳转:
      when(io.jump_en) {
        // 如果此时 r.fire, 立即丢弃并回到 s_idle
        io.axi.r.ready := true.B  // 强制接收丢弃
        when(io.axi.r.fire) {
          state := s_idle
        }.otherwise {
          // PMEM 还没返回, 进入 s_flush 等待并丢弃
          state := s_flush
        }
      }
      // 无跳转: r.fire 时直接输出
      .elsewhen(io.axi.r.valid) {
        io.out.valid := true.B
        io.out.bits.pc := reqPcReg
        io.out.bits.inst := io.axi.r.bits.data
        io.out.bits.inst_valid := true.B

        when(io.out.fire) {
          // 下游接收成功, 回到 s_idle 取下一条指令
          state := s_idle
        }
      }
    }

    // flush状态: ifu成功AR握手, 但是发生跳转, 等待并丢弃PMEM的R响应.
    is(s_flush) {
      // 等待 PMEM R 通道返回过期数据, 接受但丢弃, 然后回到 s_idle
      io.axi.r.ready := true.B
      io.out.valid := false.B
      when(io.axi.r.fire) {
        state := s_idle
      }
    }
  }

}

//AXI4协议要求valid不可以依赖ready. 即使对于两个不同通道之间, 也不应当让通道A的valid依赖通道B的ready. 因为AXI协议强制约定, valid一旦拉高, fire之前不可以自主拉低. 如果valid依赖某个ready, 不能保证这件事.

// 关于Decoupled:
//  Decoupled 本身已经自带了方向属性, 它是一个专门为"生产者(Source)"设计的模板. 不需要写 Input/Output.
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
