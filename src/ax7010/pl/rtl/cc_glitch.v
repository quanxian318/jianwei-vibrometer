// Accepts a new level only after it was stable for STABLE consecutive clocks.
`timescale 1ns / 1ps
module cc_glitch #(
    parameter WIDTH  = 1,
    parameter STABLE = 4,
    parameter [WIDTH-1:0] INIT = {WIDTH{1'b0}}
) (
    input  wire             clk,
    input  wire [WIDTH-1:0] d,
    output reg  [WIDTH-1:0] q = INIT
);
    genvar i;
    generate
        for (i = 0; i < WIDTH; i = i + 1) begin : g_bit
            reg [7:0] cnt = 8'd0;
            always @(posedge clk) begin
                if (d[i] == q[i]) begin
                    cnt <= 8'd0;
                end else if (cnt == STABLE - 1) begin
                    q[i] <= d[i];
                    cnt  <= 8'd0;
                end else begin
                    cnt <= cnt + 8'd1;
                end
            end
        end
    endgenerate
endmodule
