module RegFileSync(
  // 在scala代码(顶层模块连接, MinRV.scala)中, 例化这个模块val regSync = Module(new RegFileSync)之后, 对于gpr的连接可以直接写:
  // `regSync.gpr := gprfile.io.gpr_sync` 其中`gprfile.io.gpr_sync`就是GPR寄存器引出来的一组导线, 专门用来连接到本DPIC硬件模块. 
  // 这是因为chisel的Bundle解释向量信号的时候会展开信号名称为xxx_0, xxx_1, ..., 这里正好对上.
  input         clock,
  input  [31:0] gpr_0,  gpr_1,  gpr_2,  gpr_3,
  input  [31:0] gpr_4,  gpr_5,  gpr_6,  gpr_7,
  input  [31:0] gpr_8,  gpr_9,  gpr_10, gpr_11,
  input  [31:0] gpr_12, gpr_13, gpr_14, gpr_15,
  input  [31:0] gpr_16, gpr_17, gpr_18, gpr_19,
  input  [31:0] gpr_20, gpr_21, gpr_22, gpr_23,
  input  [31:0] gpr_24, gpr_25, gpr_26, gpr_27,
  input  [31:0] gpr_28, gpr_29, gpr_30, gpr_31,
  input  [31:0] csr_mstatus,
  input  [31:0] csr_mtvec,
  input  [31:0] csr_mepc,
  input  [31:0] csr_mcause,
  input  [31:0] csr_mcycle,
  input  [31:0] csr_mcycleh,
  input  [31:0] csr_mvendorid,
  input  [31:0] csr_marchid
);

  import "DPI-C" function void set_cpu_reg(input int idx, input int value);
  import "DPI-C" function void set_cpu_csr(input int idx, input int value);

  //在时钟周期上沿, 同步所有GPR和CSR寄存器的值到 C++ 仿真环境. 这样 C++ 端可以随时访问任意寄存器的值.
  always @(posedge clock) begin
    set_cpu_reg(0,  gpr_0);  set_cpu_reg(1,  gpr_1);
    set_cpu_reg(2,  gpr_2);  set_cpu_reg(3,  gpr_3);
    set_cpu_reg(4,  gpr_4);  set_cpu_reg(5,  gpr_5);
    set_cpu_reg(6,  gpr_6);  set_cpu_reg(7,  gpr_7);
    set_cpu_reg(8,  gpr_8);  set_cpu_reg(9,  gpr_9);
    set_cpu_reg(10, gpr_10); set_cpu_reg(11, gpr_11);
    set_cpu_reg(12, gpr_12); set_cpu_reg(13, gpr_13);
    set_cpu_reg(14, gpr_14); set_cpu_reg(15, gpr_15);
    set_cpu_reg(16, gpr_16); set_cpu_reg(17, gpr_17);
    set_cpu_reg(18, gpr_18); set_cpu_reg(19, gpr_19);
    set_cpu_reg(20, gpr_20); set_cpu_reg(21, gpr_21);
    set_cpu_reg(22, gpr_22); set_cpu_reg(23, gpr_23);
    set_cpu_reg(24, gpr_24); set_cpu_reg(25, gpr_25);
    set_cpu_reg(26, gpr_26); set_cpu_reg(27, gpr_27);
    set_cpu_reg(28, gpr_28); set_cpu_reg(29, gpr_29);
    set_cpu_reg(30, gpr_30); set_cpu_reg(31, gpr_31);
    set_cpu_csr(0, csr_mstatus);
    set_cpu_csr(1, csr_mtvec);
    set_cpu_csr(2, csr_mepc);
    set_cpu_csr(3, csr_mcause);
    set_cpu_csr(4, csr_mcycle);
    set_cpu_csr(5, csr_mcycleh);
    set_cpu_csr(6, csr_mvendorid);
    set_cpu_csr(7, csr_marchid);
  end

endmodule

