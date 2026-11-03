// Two-channel PWM for the low-side light drivers (UCC27517, high = on).
// period/duty take effect at the start of the next period, so a new setting
// never produces a runt pulse. duty >= period gives 100 %; kill forces low.
`timescale 1ns / 1ps
module cc_pwm (
    input  wire        clk,
    input  wire        rst,
    input  wire [23:0] period,   // clock cycles, >= 2
    input  wire [23:0] duty0,
    input  wire [23:0] duty1,
    input  wire        en0,
    input  wire        en1,
    input  wire        kill,
    output reg         out0 = 1'b0,
    output reg         out1 = 1'b0
);
    reg [23:0] cnt = 24'd0;
    reg [23:0] per_q = 24'd2;
    reg [23:0] d0_q = 24'd0, d1_q = 24'd0;
    reg        e0_q = 1'b0, e1_q = 1'b0;

    wire wrap = (cnt + 24'd1 >= per_q);
    always @(posedge clk) begin
        if (rst) begin
            cnt  <= 24'd0;
            per_q <= (period < 24'd2) ? 24'd2 : period;
            d0_q <= 24'd0;
            d1_q <= 24'd0;
            e0_q <= 1'b0;
            e1_q <= 1'b0;
        end else if (wrap) begin
            cnt  <= 24'd0;
            per_q <= (period < 24'd2) ? 24'd2 : period;
            d0_q <= duty0;
            d1_q <= duty1;
            e0_q <= en0;
            e1_q <= en1;
        end else begin
            cnt <= cnt + 24'd1;
        end
        // Registered outputs; kill acts on the next clock regardless of the period.
        out0 <= ~rst & ~kill & en0 & e0_q & (cnt < d0_q);
        out1 <= ~rst & ~kill & en1 & e1_q & (cnt < d1_q);
    end
endmodule
