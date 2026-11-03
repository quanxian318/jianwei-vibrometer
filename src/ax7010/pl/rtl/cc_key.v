// Debounced push button (contact closes to GND, input already synchronized).
// pressed changes only after the input held the new level for debounce_ms.
// long_pulse fires once when the key has been held for long_ms.
`timescale 1ns / 1ps
module cc_key (
    input  wire        clk,
    input  wire        rst,
    input  wire        tick_ms,
    input  wire        level,        // synchronized pin level, 0 = pressed
    input  wire [7:0]  debounce_ms,
    input  wire [15:0] long_ms,
    output reg         pressed = 1'b0,
    output reg         long_pulse = 1'b0
);
    reg [7:0]  deb_cnt = 8'd0;
    reg [15:0] hold_cnt = 16'd0;
    reg        long_done = 1'b0;

    always @(posedge clk) begin
        long_pulse <= 1'b0;
        if (rst) begin
            pressed   <= 1'b0;
            deb_cnt   <= 8'd0;
            hold_cnt  <= 16'd0;
            long_done <= 1'b0;
        end else begin
            // Debounce: count milliseconds while the input disagrees with the state.
            if (~level == pressed) begin
                deb_cnt <= 8'd0;
            end else if (tick_ms) begin
                if (deb_cnt + 8'd1 >= debounce_ms) begin
                    pressed <= ~level;
                    deb_cnt <= 8'd0;
                end else begin
                    deb_cnt <= deb_cnt + 8'd1;
                end
            end
            // Long press: one pulse per press.
            if (!pressed) begin
                hold_cnt  <= 16'd0;
                long_done <= 1'b0;
            end else if (tick_ms && !long_done) begin
                if (hold_cnt + 16'd1 >= long_ms) begin
                    long_pulse <= 1'b1;
                    long_done  <= 1'b1;
                end else begin
                    hold_cnt <= hold_cnt + 16'd1;
                end
            end
        end
    end
endmodule
