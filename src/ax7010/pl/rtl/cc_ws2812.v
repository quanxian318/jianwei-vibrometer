// WS2812B-V6 chain driver (HL101 first). Each LED takes 24 bits G,R,B, MSB
// first, 1.27 us per bit; a frame ends with the line held low for at least
// reset_us (>= 300 us for this project). Colors are latched when a frame
// starts, so register writes during a frame never tear it.
`timescale 1ns / 1ps
module cc_ws2812 #(
    parameter CLK_HZ = 100000000,
    parameter NLED   = 5
) (
    input  wire              clk,
    input  wire              rst,
    input  wire              start,           // request one frame
    input  wire [24*NLED-1:0] grb,            // LED0 in the low 24 bits
    input  wire [15:0]       reset_us,
    input  wire              tick_us,
    output reg               dout = 1'b0,
    output wire              busy,
    output reg               pending = 1'b0,
    output reg  [15:0]       frames = 16'd0
);
    // Bit timing from the clock: 0 code 0.32/0.95 us, 1 code 0.64/0.63 us,
    // about 50 ns inside the V6 limits (T0H 0.22-0.38 us, T1H/T0L/T1L 0.58-1 us).
    localparam integer T_BIT = (CLK_HZ / 1000 * 1270 + 500000) / 1000000;
    localparam integer T_0H  = (CLK_HZ / 1000 * 320 + 500000) / 1000000;
    localparam integer T_1H  = (CLK_HZ / 1000 * 640 + 500000) / 1000000;
    localparam NBITS = 24 * NLED;

    localparam S_IDLE = 2'd0, S_BIT = 2'd1, S_RESET = 2'd2;
    reg [1:0]  state = S_IDLE;
    reg [7:0]  led_idx = 8'd0;
    reg [4:0]  bit_idx = 5'd0;
    reg [15:0] tcnt = 16'd0;
    reg [15:0] rcnt = 16'd0;
    reg [23:0] cur = 24'd0;
    reg [NBITS-1:0] latched = {NBITS{1'b0}};

    assign busy = (state != S_IDLE);

    always @(posedge clk) begin
        if (rst) begin
            state   <= S_IDLE;
            dout    <= 1'b0;
            pending <= 1'b0;
            frames  <= 16'd0;
        end else begin
            if (start) pending <= 1'b1;
            case (state)
                S_IDLE: begin
                    dout <= 1'b0;
                    if (start || pending) begin
                        pending <= 1'b0;
                        latched <= grb;
                        cur     <= grb[23:0];
                        led_idx <= 8'd0;
                        bit_idx <= 5'd23;
                        tcnt    <= 16'd0;
                        state   <= S_BIT;
                    end
                end
                S_BIT: begin
                    // High for T0H/T1H, low for the rest of T_BIT.
                    dout <= (tcnt < (cur[bit_idx] ? T_1H : T_0H));
                    if (tcnt == T_BIT - 1) begin
                        tcnt <= 16'd0;
                        if (bit_idx == 5'd0) begin
                            bit_idx <= 5'd23;
                            if (led_idx == NLED - 1) begin
                                state <= S_RESET;
                                rcnt  <= 16'd0;
                            end else begin
                                led_idx <= led_idx + 8'd1;
                                cur     <= latched >> (24 * (led_idx + 1));
                            end
                        end else begin
                            bit_idx <= bit_idx - 5'd1;
                        end
                    end else begin
                        tcnt <= tcnt + 16'd1;
                    end
                end
                S_RESET: begin
                    dout <= 1'b0;
                    // The first tick may come right away: counting reset_us + 1
                    // ticks keeps the line low for at least reset_us.
                    if (tick_us) begin
                        if (rcnt >= reset_us) begin
                            state  <= S_IDLE;
                            frames <= frames + 16'd1;
                        end else begin
                            rcnt <= rcnt + 16'd1;
                        end
                    end
                end
                default: state <= S_IDLE;
            endcase
        end
    end
endmodule
