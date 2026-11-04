module PMEMWrite(
  input         clock,
  input         wen,
  input  [31:0] waddr,
  input  [31:0] wdata,
  input  [3:0]  wmask
);

  import "DPI-C" function void pmem_write(
    input int waddr,
    input int wdata,
    input byte wmask
  );

  always @(posedge clock) begin
    if (wen) begin
      pmem_write({waddr[31:2], 2'b00}, wdata, {4'b0, wmask});
    end
  end

endmodule

