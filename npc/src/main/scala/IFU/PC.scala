// MiniRV 程序计数器 (Program Counter)
package minirv.ifu

import chisel3._
import chisel3.util._
import minirv._

/**
  * PC - 程序计数器
  * 
  * 功能：
  * 1. 维护当前 PC 值
  * 2. 根据控制信号更新 PC (顺序 +4 或跳转)
  */
class PC extends Module {
  val io = IO(new Bundle {
    // PC 更新控制
    val jump_en   = Input(Bool())                       // 跳转使能
    val jump_addr = Input(UInt(Config.ADDR_WIDTH.W))    // 跳转目标地址
    val stall     = Input(Bool())                       // 流水线暂停信号. 实际是PC寄存器的使能.
    
    // 当前 PC 输出
    val pc        = Output(UInt(Config.ADDR_WIDTH.W))   // 当前 PC 值
    val next_pc   = Output(UInt(Config.ADDR_WIDTH.W))   // 下一条 PC (用于调试)
  })

  // PC 寄存器，初始值为 0x80000000 (RISC-V 典型复位地址)
  val pc_reg = RegInit("h80000000".U(Config.ADDR_WIDTH.W))

  // 计算下一条 PC
  val next_pc_val = Mux(io.jump_en, io.jump_addr, pc_reg + 4.U)

  // 更新 PC 寄存器（stall 时保持不变）
  when(!io.stall) {
    pc_reg := next_pc_val
  }

  // 输出
  io.pc      := pc_reg
  io.next_pc := next_pc_val
}





//教学......

//关于阻塞赋值和非阻塞赋值:
//在 Chisel 中，我们不需要关心阻塞 (Blocking, =) 还是非阻塞 (Non-Blocking, <=) 赋值的语法选择，因为 Chisel 已经将这个选择抽象化了。

// 关于clk和rst: 
// 所有Module对象都有clock和reset成员. 它们被隐式连接: 
    // 子模块自动继承父模块的clock和reset信号.
    // 时序模块回直接使用这些成员, 而组合逻辑模块忽略它们.


// Mux(cond, a, b)是一个选择器，如果 cond 为 true，则输出 a，否则输出 b.
    // 实现细节: scala允许一个apply语法糖: 如果myclass有一个apply()方法, 那麽myclass()等价于myclass.apply()
    // 最终, Mux(cond, a, b)是个表达式. 调用Mux的apply方法, 返回值是一个Chisel3中的 Data 对象. 这个Data代表电路中的一个节点, 在elaboration阶段, 会被转换成硬件描述语言中的一个节点.

// := 是Chisel3中的赋值操作符. 方向是「右边驱动左边」.



/**********************************工厂函数范式: 以UInt为例************************************/
// val in  = Input(UInt(4.W))    UInt(4.W)这个表达式到底做了什麽?
// 首先(证明略),  4.W 等价于 Width(4), 这是一个隐式转换.
    // 简略解释:
// 编译器看到UInt(), 于是认为调用了UInt这个class的伴生对象的apply方法. 
// 即: UInt(4.W) ≡ UInt.apply(4.W) 
// chisel3命名空间中, 存在class UInt, 以及它的伴生对象Object UInt.
// 显然, 这个 Object UInt对象应当有一个apply方法, 它接收形如4.W这样的参数, 然后new一个Data对象.
// 事实上, 这个 Object UInt 没有自己实现一个apply方法, 而是通过 with 了一个工厂对象: trait UIntFactory {...(可以从UInt跳转), 这个工厂对象内有一个apply方法: 
    //  /** Create a UInt port with specified width. */
    //  def apply(width: Width): UInt = new UInt(width)
    // 这个方法做的事: new了一个UInt实例, 返回值就是这个实例.

// 于是一切都清楚了. UInt(4.w)表达式返回一个new UInt实例.
// 它什麽时候发生呢? 当class PC在某处被new出来一个实例时, 初始化该pc实例时, 这一句就会被调用, new一个UInt实例.
// 于是, 通过这种方式, UInt(4.w)实际上就是调用了方法: UIntFactory.apply(4.W). 
// 这样做的好处: 首先可以看着好看: UInt(4.W)看起来好像直接用了UInt的apply, 但其实不是.  
// 这样做是为了复用工厂函数UIntFactory. 它可能会被用在很多地方, 伪装成那些对象的allpy!
/************************************************************************************/


//
// 











