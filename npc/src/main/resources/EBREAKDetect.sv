module EBREAKDetect(
  input         clock,
  input  [31:0] inst,
  input         valid
);

  import "DPI-C" function void ebreak_handler();

  always @(posedge clock) begin
    if (valid && inst == 32'h00100073) begin
      ebreak_handler();
    end
  end

endmodule

