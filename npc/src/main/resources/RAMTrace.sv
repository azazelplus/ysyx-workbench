// 观察当前 Arbiter 的全局单笔、INCR 事务；不会驱动 AXI 或修改 RAM。
// AR/AW 保存物理地址，R/W 每次握手后按 AxSIZE 递增，避免反压期间重复记录。
module RAMTrace (
  input clock,
  input reset,
  input ar_fire,
  input [31:0] ar_addr,
  input [2:0] ar_size,
  input r_fire,
  input [31:0] r_data,
  input aw_fire,
  input [31:0] aw_addr,
  input [2:0] aw_size,
  input w_fire,
  input [31:0] w_data,
  input [3:0] w_strb
);
  import "DPI-C" function void ram_trace_read(input int unsigned addr, data);
  import "DPI-C" function void ram_trace_write(
    input int unsigned addr, data, input byte unsigned mask);

  reg [31:0] read_addr, write_addr;
  reg [2:0] read_size, write_size;

  always @(posedge clock) begin
    if (reset) begin
      read_addr <= 0;
      write_addr <= 0;
      read_size <= 0;
      write_size <= 0;
    end else begin
      if (ar_fire) begin
        read_addr <= ar_addr;
        read_size <= ar_size;
      end
      if (r_fire) begin
        ram_trace_read(read_addr, r_data);
        read_addr <= read_addr + (32'd1 << read_size);
      end
      if (aw_fire) begin
        write_addr <= aw_addr;
        write_size <= aw_size;
      end
      if (w_fire) begin
        // AW、首个 W 同拍握手时，使用当前 AW 地址。
        ram_trace_write(aw_fire ? aw_addr : write_addr, w_data, {4'b0, w_strb});
        write_addr <= (aw_fire ? aw_addr : write_addr) +
                      (32'd1 << (aw_fire ? aw_size : write_size));
      end
    end
  end
endmodule
