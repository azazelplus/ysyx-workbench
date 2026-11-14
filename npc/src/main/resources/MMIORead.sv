module MMIORead(
  input         clock,
  input         ren,
  input  [31:0] raddr,
  output reg [31:0] rdata
);
  import "DPI-C" function int unsigned mmio_read(input int unsigned raddr);

  // AR 握手时只读取一次设备。R 通道被反压时，保持这次读取的结果。
  always @(posedge clock) begin
    if (ren) begin
      rdata <= mmio_read({raddr[31:2], 2'b00});
    end
  end
endmodule
