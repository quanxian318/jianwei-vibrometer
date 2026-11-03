// Two-flop synchronizer for asynchronous single-bit inputs (one per bit).
`timescale 1ns / 1ps
module cc_sync #(
    parameter WIDTH = 1,
    parameter [WIDTH-1:0] INIT = {WIDTH{1'b0}}
) (
    input  wire             clk,
    input  wire [WIDTH-1:0] d,
    output wire [WIDTH-1:0] q
);
    (* ASYNC_REG = "TRUE" *) reg [WIDTH-1:0] s1 = INIT;
    (* ASYNC_REG = "TRUE" *) reg [WIDTH-1:0] s2 = INIT;
    always @(posedge clk) begin
        s1 <= d;
        s2 <= s1;
    end
    assign q = s2;
endmodule
