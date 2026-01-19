// DPI-C 存储器访问接口
// 通过 DPI-C 机制与 C++ 仿真环境交互.
// PMEMRead内存访问接口 input: raddr  output: rdata
// PMEMWrite内存写入接口 input: wen, waddr, wdata,
// EBREAKDetect EBREAK指令检测接口 input: inst, valid





// DPI-C(direct programming interface-c) 是一种用于将system verilog和c/c++代码进行交互的技术.
// 它允许verilog代码直接调用c/c++函数，反之亦然.
// 它允许你在 Verilog 代码里直接写 pmem_read(...)，然后仿真器（比如 Verilator）在运行时，会跳出去执行你写好的 C++ 函数，拿回结果后再跳回硬件世界.




package minirv

import chisel3._
import chisel3.util._

/**
  * 内存模块(DPI-C 存储器读取模块) Phy-MEM. 组合瞬间访问.
  * 
  * 使用 Chisel 的 BlackBox 机制，定义外部 DPI-C 函数接口。
  * 实际的函数实现在 C++ 代码中。
  * 
  * pmem_read: 从存储器读取 32 位数据
  *   - raddr: 读地址（需按 4 字节对齐）
  *   - 返回: 32 位数据
  */
// BlackBox 是 Chisel类, 用于定义与外部模块的接口.
// HasBlackBoxInline 是 Chisel trait, 用于在 Verilog 代码中内联 SystemVerilog 代码.
class PMEMRead extends BlackBox with HasBlackBoxInline {
  val io = IO(new Bundle {
    val clock  = Input(Clock())
    val raddr  = Input(UInt(32.W))   // 读地址
    val rdata  = Output(UInt(32.W))  // 读数据
  })

  // 内联 SystemVerilog 代码，使用 DPI-C 调用. stripMargin是字符串的对齐方法(用内部`|`实现. stripMargin方法会删除字符串每行开头直到`|`之前的所有缩进.同时清除`|`.)
  setInline("PMEMRead.sv",
    """
      |module PMEMRead(
      |  input         clock,
      |  input  [31:0] raddr,
      |  output [31:0] rdata
      |);
      |
      |  // DPI-C 函数声明
      |  // 注意: DPI-C function的参数和返回值类型需要与 C++ 端一致.
      |  // SV的 int unsigned 对应 C++ 的 unsigned int.
      |  // 事实上很多示例用 SV的int对应C++的int(导致C++中为了处理地址, 要强行转换一次到uint). 这是历史原因...
      |  import "DPI-C" function int unsigned pmem_read(input int unsigned raddr);
      |
      |  // 调用 DPI-C 函数读取存储器
      |  // 地址按 4 字节对齐
      |  // assign rdata = pmem_read({raddr[31:2], 2'b00});   // 如果你想让寄存器瞬间访问而不是单周期...
      |  assign rdata = pmem_read({raddr[31:2], 2'b00});
      |
      |endmodule
      |
      |""".stripMargin)
}



/**
  * DPI-C 存储器写入模块
  * 
  * pmem_write: 向存储器写入数据
  *   - waddr: 写地址（需按 4 字节对齐）
  *   - wdata: 写数据
  *   - wmask: 写掩码(按字节，4 位)
  */
class PMEMWrite extends BlackBox with HasBlackBoxInline {
  val io = IO(new Bundle {
    val clock  = Input(Clock())
    val wen    = Input(Bool())       // 写使能
    val waddr  = Input(UInt(32.W))   // 写地址
    val wdata  = Input(UInt(32.W))   // 写数据
    val wmask  = Input(UInt(4.W))    // 写掩码（按字节）
  })

  // 内联 SystemVerilog 代码
  setInline("PMEMWrite.sv",
    """module PMEMWrite(
      |  input         clock,
      |  input         wen,
      |  input  [31:0] waddr,
      |  input  [31:0] wdata,
      |  input  [3:0]  wmask
      |);

      |  // DPI-C 函数声明
      |  // 注意: DPI-C 函数的参数和返回值类型需要与 C++ 端一致.
      |  import "DPI-C" function void pmem_write(
      |    input int waddr,
      |    input int wdata,
      |    input byte wmask
      |  );
      |
      |  // 在时钟上升沿且写使能有效时，调用 DPI-C 函数
      |  always @(posedge clock) begin
      |    if (wen) begin
      |      pmem_write({waddr[31:2], 2'b00}, wdata, {4'b0, wmask});
      |    end
      |  end
      |
      |endmodule
      |
      |""".stripMargin)
}

/**
  * EBREAK 检测模块
  * 用于在仿真中检测 EBREAK 指令并终止仿真
  */
class EBREAKDetect extends BlackBox with HasBlackBoxInline {
  val io = IO(new Bundle {
    val clock   = Input(Clock())
    val inst    = Input(UInt(32.W))  // 当前指令
    val valid   = Input(Bool())      // 指令有效
  })

  setInline("EBREAKDetect.sv",
    """module EBREAKDetect(
      |  input         clock,
      |  input  [31:0] inst,
      |  input         valid
      |);
      |
      |  // DPI-C 函数声明
      |  import "DPI-C" function void ebreak_handler();
      |
      |  // EBREAK 指令编码: 0x00100073
      |  always @(posedge clock) begin
      |    if (valid && inst == 32'h00100073) begin
      |      ebreak_handler();
      |    end
      |  end
      |
      |endmodule
      |
      |""".stripMargin)
}

/**
  * 寄存器堆同步模块. 它接收regfile的32个寄存器输入, 没有输出. 它在内部: 每个时钟周期, 将所有 32 个寄存器的值同步到 C++ 仿真环境. Difftest!
这样 C++ 端可以随时访问任意寄存器的值.
  */
class RegFileSync extends BlackBox with HasBlackBoxInline {
  val io = IO(new Bundle {
    val clock = Input(Clock())
    val regs  = Input(Vec(32, UInt(32.W)))  // 32 个寄存器
  })

  setInline("RegFileSync.sv",
    """module RegFileSync(
      |  input         clock,
      |  input  [31:0] regs_0,  regs_1,  regs_2,  regs_3,
      |  input  [31:0] regs_4,  regs_5,  regs_6,  regs_7,
      |  input  [31:0] regs_8,  regs_9,  regs_10, regs_11,
      |  input  [31:0] regs_12, regs_13, regs_14, regs_15,
      |  input  [31:0] regs_16, regs_17, regs_18, regs_19,
      |  input  [31:0] regs_20, regs_21, regs_22, regs_23,
      |  input  [31:0] regs_24, regs_25, regs_26, regs_27,
      |  input  [31:0] regs_28, regs_29, regs_30, regs_31
      |);
      |
      |  // DPI-C 函数声明: 同步单个寄存器值到 C++ 端
      |  import "DPI-C" function void set_cpu_reg(input int idx, input int value);
      |
      |  // 每个时钟上升沿同步所有寄存器
      |  always @(posedge clock) begin
      |    set_cpu_reg(0,  regs_0);  set_cpu_reg(1,  regs_1);
      |    set_cpu_reg(2,  regs_2);  set_cpu_reg(3,  regs_3);
      |    set_cpu_reg(4,  regs_4);  set_cpu_reg(5,  regs_5);
      |    set_cpu_reg(6,  regs_6);  set_cpu_reg(7,  regs_7);
      |    set_cpu_reg(8,  regs_8);  set_cpu_reg(9,  regs_9);
      |    set_cpu_reg(10, regs_10); set_cpu_reg(11, regs_11);
      |    set_cpu_reg(12, regs_12); set_cpu_reg(13, regs_13);
      |    set_cpu_reg(14, regs_14); set_cpu_reg(15, regs_15);
      |    set_cpu_reg(16, regs_16); set_cpu_reg(17, regs_17);
      |    set_cpu_reg(18, regs_18); set_cpu_reg(19, regs_19);
      |    set_cpu_reg(20, regs_20); set_cpu_reg(21, regs_21);
      |    set_cpu_reg(22, regs_22); set_cpu_reg(23, regs_23);
      |    set_cpu_reg(24, regs_24); set_cpu_reg(25, regs_25);
      |    set_cpu_reg(26, regs_26); set_cpu_reg(27, regs_27);
      |    set_cpu_reg(28, regs_28); set_cpu_reg(29, regs_29);
      |    set_cpu_reg(30, regs_30); set_cpu_reg(31, regs_31);
      |  end
      |
      |endmodule
      |
      |""".stripMargin)
}
