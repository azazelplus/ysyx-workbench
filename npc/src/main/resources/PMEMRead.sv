module PMEMRead(
  input         clock,
  input  [31:0] raddr,
  output [31:0] rdata
);
  // DPI-C 函数声明
  // 注意: DPI-C function的参数和返回值类型需要与 C++ 端一致.
  // SV的 int unsigned 对应 C++ 的 unsigned int.
  // 事实上很多示例用 SV的int对应C++的int(导致C++中为了处理地址, 要强行转换一次到uint). 这是历史原因...
  import "DPI-C" function int unsigned pmem_read(input int unsigned raddr);

  // 调用 DPI-C 函数读取存储器
  // 地址按 4 字节对齐
  // assign rdata = pmem_read({raddr[31:2], 2'b00});   // 如果你想让寄存器瞬间访问而不是单周期...
  assign rdata = pmem_read({raddr[31:2], 2'b00});

endmodule

