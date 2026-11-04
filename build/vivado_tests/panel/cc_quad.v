// Quadrature decoder for the EC11 encoder (A/B already synchronized).
// Each channel must be stable for filter_us microseconds before it is used;
// only single-bit Gray transitions count, double transitions are errors.
`timescale 1ns / 1ps
module cc_quad (
    input  wire        clk,
    input  wire        rst,
    input  wire        tick_us,
    input  wire        a,
    input  wire        b,
    input  wire [9:0]  filter_us,
    output reg  signed [31:0] count = 32'sd0,
    output reg  [31:0] errors = 32'd0,
    output reg         step = 1'b0,     // one-clock pulse per legal transition
    output reg         step_dir = 1'b0, // 1 = count up
    output wire        a_filt,
    output wire        b_filt
);
    reg [9:0] cnt_a = 10'd0, cnt_b = 10'd0;
    reg fa = 1'b1, fb = 1'b1;
    reg [1:0] prev = 2'b11;
    assign a_filt = fa;
    assign b_filt = fb;

    always @(posedge clk) begin
        if (rst) begin
            cnt_a <= 10'd0;
            cnt_b <= 10'd0;
            fa <= a;
            fb <= b;
        end else begin
            if (a == fa) cnt_a <= 10'd0;
            else if (tick_us) begin
                if (cnt_a + 10'd1 >= filter_us) begin
                    fa <= a;
                    cnt_a <= 10'd0;
                end else cnt_a <= cnt_a + 10'd1;
            end
            if (b == fb) cnt_b <= 10'd0;
            else if (tick_us) begin
                if (cnt_b + 10'd1 >= filter_us) begin
                    fb <= b;
                    cnt_b <= 10'd0;
                end else cnt_b <= cnt_b + 10'd1;
            end
        end
    end

    wire [1:0] cur = {fa, fb};
    always @(posedge clk) begin
        step <= 1'b0;
        if (rst) begin
            prev   <= {fa, fb};
            count  <= 32'sd0;
            errors <= 32'd0;
        end else if (cur != prev) begin
            prev <= cur;
            case ({prev, cur})
                // A leads B: 00 -> 10 -> 11 -> 01 -> 00
                4'b0010, 4'b1011, 4'b1101, 4'b0100: begin
                    count    <= count + 32'sd1;
                    step     <= 1'b1;
                    step_dir <= 1'b1;
                end
                // B leads A: 00 -> 01 -> 11 -> 10 -> 00
                4'b0001, 4'b0111, 4'b1110, 4'b1000: begin
                    count    <= count - 32'sd1;
                    step     <= 1'b1;
                    step_dir <= 1'b0;
                end
                default: if (errors != 32'hFFFF_FFFF) errors <= errors + 32'd1;
            endcase
        end
    end
endmodule
