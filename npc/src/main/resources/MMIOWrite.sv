module MMIOWrite(
  input         clock,
  input         wen,
  input  [31:0] waddr,
  input  [31:0] wdata,
  input  [3:0]  wmask
);
  import "DPI-C" function void mmio_write(
    input int unsigned waddr,
    input int unsigned wdata,
    input byte unsigned wmask
  );

  // 一笔写事务只调用一次设备后端，B 通道被反压时不重复写入。
  always @(posedge clock) begin
    if (wen) begin
      mmio_write({waddr[31:2], 2'b00}, wdata, {4'b0, wmask});
    end
  end
endmodule
