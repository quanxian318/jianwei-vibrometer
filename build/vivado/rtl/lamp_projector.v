// J301 PWM lamp and J304 constant-power projector; independent enables.
// Command: bit0 lamp, bit1 projector, [14:8] lamp percent, bit16 heartbeat.
// Default/reset/PFAIL/missing heartbeat all force the output low. SW301 remains
// a separate physical interlock in the carrier driver circuit.
`timescale 1ns/1ps
module lamp_projector #(
    parameter CLK_HZ = 100000000,
    parameter PWM_HZ = 20000,
    parameter TIMEOUT_MS = 2000
) (
    input wire clk,
    input wire resetn,
    input wire [31:0] command,
    input wire pfail_n,
    output wire led1_pwm,
    output wire led2_pwm,
    output wire [31:0] status
);
    localparam PERIOD = CLK_HZ / PWM_HZ;
    localparam CW = $clog2(PERIOD);
    localparam TIMEOUT = (CLK_HZ / 1000) * TIMEOUT_MS;
    localparam WW = $clog2(TIMEOUT + 1);
    (* ASYNC_REG = "TRUE" *) reg [1:0] pf_sync = 0;
    reg hb = 0;
    reg alive = 0;
    reg [WW-1:0] age = 0;
    reg [CW-1:0] count = 0;
    reg [CW:0] duty = 0;
    wire [6:0] percent = command[14:8] > 100 ? 7'd100 : command[14:8];
    wire healthy = alive && pf_sync[1];
    // Constant product; no divider, multipliers or frame memory.
    always @(posedge clk) begin
        pf_sync <= {pf_sync[0], pfail_n};
        if (!resetn) begin
            hb <= command[16]; alive <= 0; age <= 0;
            count <= 0; duty <= 0;
        end else begin
            if (command[16] != hb) begin
                hb <= command[16]; age <= 0; alive <= 1;
            end else if (age < TIMEOUT) age <= age + 1'b1;
            else alive <= 0;
            if (count == PERIOD-1) begin
                count <= 0;
                duty <= percent * (PERIOD / 100);
            end else count <= count + 1'b1;
        end
    end
    assign led1_pwm = resetn && healthy && command[0] && (count < duty);
    // Projector has its own driver; never chop its power with lamp brightness.
    assign led2_pwm = resetn && healthy && command[1];
    // Distinct from carrier_ctrl: cannot be mistaken for the full carrier core.
    assign status = 32'h4c503200 | {28'd0, led2_pwm, led1_pwm, alive, pf_sync[1]};
endmodule
