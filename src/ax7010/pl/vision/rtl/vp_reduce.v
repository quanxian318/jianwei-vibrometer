// Streaming pyramid REDUCE: 5-tap binomial low-pass [1 4 6 4 1]/16 on both
// axes with edge replication, then 2x decimation keeping even rows/columns.
// Bit-exact with pyrReduce() in vision.cpp: out = (sum + 128) >> 8.
//
// At most one input pixel per clock, never back-pressures. The input width
// must be even; the height may be odd, in which case the last output row is
// produced from the line buffers right after the frame (needs w/2 idle
// clocks before the next frame; `overrun` flags a violation).
`timescale 1ns / 1ps
module vp_reduce #(
    parameter MAXW = 640               // maximum input width
) (
    input  wire        clk,
    input  wire        rst,
    input  wire [15:0] height,         // input rows per frame
    input  wire        in_valid,
    input  wire [7:0]  in_data,
    input  wire        in_sof,         // first pixel of a frame
    input  wire        in_eol,         // last pixel of a row
    output reg         out_valid = 1'b0,
    output reg  [7:0]  out_data = 8'd0,
    output reg         out_sof = 1'b0,
    output reg         out_eol = 1'b0,
    output reg         out_eof = 1'b0,
    output reg         overrun = 1'b0
);
    localparam HW = MAXW / 2;
    localparam AW = (HW <= 2) ? 1 : $clog2(HW);

    // ------------------------------------------------------------------
    // S1: position counters and the horizontal 5-pixel window
    // ------------------------------------------------------------------
    reg [15:0] col = 16'd0, row = 16'd0;
    wire [15:0] cur_col = in_sof ? 16'd0 : col;
    wire [15:0] cur_row = in_sof ? 16'd0 : row;
    reg [7:0] s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0;
    reg        h_fire = 1'b0, h_eol = 1'b0;
    reg [AW-1:0] h_ox = 0;
    reg [15:0] h_row = 16'd0;

    always @(posedge clk) begin
        h_fire <= 1'b0;
        if (rst) begin
            col <= 16'd0;
            row <= 16'd0;
        end else if (in_valid) begin
            if (cur_col == 16'd0) begin
                {s4, s3, s2, s1} <= {4{in_data}};  // left edge: pixels -1..-4 = pixel 0
            end else begin
                {s4, s3, s2, s1} <= {s3, s2, s1, s0};
            end
            s0 <= in_data;
            h_fire <= (cur_col[0] == 1'b0 && cur_col >= 16'd2) || in_eol;
            h_eol <= in_eol;
            h_ox <= in_eol ? ((cur_col - 16'd1) >> 1) : ((cur_col - 16'd2) >> 1);
            h_row <= cur_row;
            if (in_eol) begin
                col <= 16'd0;
                row <= cur_row + 16'd1;
            end else begin
                col <= cur_col + 16'd1;
                row <= cur_row;  // a start of frame resets the row as well
            end
        end
    end

    // ------------------------------------------------------------------
    // S2: horizontal sum (12 bits)
    // ------------------------------------------------------------------
    reg        hv = 1'b0, hv_eol = 1'b0;
    reg [11:0] hsum = 12'd0;
    reg [AW-1:0] hv_ox = 0;
    reg [15:0] hv_row = 16'd0;
    always @(posedge clk) begin
        hv <= h_fire & ~rst;
        if (h_fire) begin
            // At the row end the right neighbours are the last pixel (replicated).
            hsum <= h_eol ? ({4'd0, s3} + {2'd0, s2, 2'd0} + {3'd0, s1, 1'b0} + {2'd0, s1, 2'd0} +
                             {2'd0, s0, 2'd0} + {4'd0, s0})
                          : ({4'd0, s4} + {2'd0, s3, 2'd0} + {3'd0, s2, 1'b0} + {2'd0, s2, 2'd0} +
                             {2'd0, s1, 2'd0} + {4'd0, s0});
            hv_eol <= h_eol;
            hv_ox <= h_ox;
            hv_row <= h_row;
        end
    end

    // ------------------------------------------------------------------
    // S3: four line buffers of horizontal sums (read-first), flush counter
    // ------------------------------------------------------------------
    reg [11:0] lb0 [0:HW-1];
    reg [11:0] lb1 [0:HW-1];
    reg [11:0] lb2 [0:HW-1];
    reg [11:0] lb3 [0:HW-1];
    reg [11:0] q0 = 0, q1 = 0, q2 = 0, q3 = 0, hd = 0;
    reg        sv = 1'b0, sv_eol = 1'b0, sv_flush = 1'b0;
    reg [AW-1:0] sv_ox = 0;
    reg [15:0] sv_row = 16'd0;

    // Odd heights: one more output row after the last input row.
    reg        f_active = 1'b0;
    reg [AW-1:0] f_ox = 0, f_last = 0;
    reg [15:0] f_row = 16'd0;
    wire       last_row_odd_h = hv && hv_eol && (hv_row == height - 16'd1) && height[0];

    wire [AW-1:0] rd_addr = f_active ? f_ox : hv_ox;
    always @(posedge clk) begin
        sv <= 1'b0;
        sv_flush <= 1'b0;
        if (rst) begin
            f_active <= 1'b0;
            overrun <= 1'b0;
        end else begin
            if (hv || f_active) begin
                q0 <= lb0[rd_addr];
                q1 <= lb1[rd_addr];
                q2 <= lb2[rd_addr];
                q3 <= lb3[rd_addr];
            end
            if (hv) begin
                case (hv_row[1:0])
                    2'd0: lb0[hv_ox] <= hsum;
                    2'd1: lb1[hv_ox] <= hsum;
                    2'd2: lb2[hv_ox] <= hsum;
                    default: lb3[hv_ox] <= hsum;
                endcase
                hd <= hsum;
                sv <= 1'b1;
                sv_eol <= hv_eol;
                sv_ox <= hv_ox;
                sv_row <= hv_row;
                if (f_active) overrun <= 1'b1;
            end
            if (f_active) begin
                sv_flush <= 1'b1;
                sv_ox <= f_ox;
                sv_row <= f_row;
                sv_eol <= (f_ox == f_last);
                if (f_ox == f_last) begin
                    f_active <= 1'b0;
                end else begin
                    f_ox <= f_ox + 1'b1;
                end
            end
            if (last_row_odd_h) begin
                f_active <= 1'b1;
                f_ox <= 0;
                f_last <= hv_ox;
                f_row <= hv_row;
            end
        end
    end

    // ------------------------------------------------------------------
    // S4: vertical sum and output
    // ------------------------------------------------------------------
    // Value of input row q from the line buffers (rows above the top = row 0).
    function [11:0] pick(input signed [17:0] q, input [11:0] a0, input [11:0] a1, input [11:0] a2,
                         input [11:0] a3);
        reg [1:0] s;
        begin
            s = (q < 0) ? 2'd0 : q[1:0];
            case (s)
                2'd0: pick = a0;
                2'd1: pick = a1;
                2'd2: pick = a2;
                default: pick = a3;
            endcase
        end
    endfunction

    wire signed [17:0] r = {2'b00, sv_row};
    wire last_row  = (sv_row == height - 16'd1);
    wire normal    = sv && !sv_row[0] && sv_row >= 16'd2;  // output row (r-2)/2
    wire last_even = sv && last_row && !height[0];          // even height: output row (r-1)/2
    wire fire = normal || last_even || sv_flush;
    // Rows y-2 .. y+2 of the output centre y.
    wire [11:0] ra = sv_flush ? pick(r - 18'sd2, q0, q1, q2, q3) :
                     (normal ? pick(r - 18'sd4, q0, q1, q2, q3) : pick(r - 18'sd3, q0, q1, q2, q3));
    wire [11:0] rb = sv_flush ? pick(r - 18'sd1, q0, q1, q2, q3) :
                     (normal ? pick(r - 18'sd3, q0, q1, q2, q3) : pick(r - 18'sd2, q0, q1, q2, q3));
    wire [11:0] rc = sv_flush ? pick(r, q0, q1, q2, q3) :
                     (normal ? pick(r - 18'sd2, q0, q1, q2, q3) : pick(r - 18'sd1, q0, q1, q2, q3));
    wire [11:0] rd = sv_flush ? pick(r, q0, q1, q2, q3) : (normal ? pick(r - 18'sd1, q0, q1, q2, q3) : hd);
    wire [11:0] re = sv_flush ? pick(r, q0, q1, q2, q3) : hd;
    wire [17:0] vsum = {6'd0, ra} + {4'd0, rb, 2'd0} + {5'd0, rc, 1'b0} + {4'd0, rc, 2'd0} + {4'd0, rd, 2'd0} +
                       {6'd0, re};
    wire [15:0] oy = sv_flush ? (sv_row >> 1) : (normal ? ((sv_row - 16'd2) >> 1) : ((sv_row - 16'd1) >> 1));
    wire [15:0] last_oy = (height - 16'd1) >> 1;

    always @(posedge clk) begin
        out_valid <= fire & ~rst;
        if (fire) begin
            out_data <= vsum[15:8] + (vsum[7] ? 8'd1 : 8'd0);  // (sum + 128) >> 8, sum <= 65280
            out_sof <= (oy == 16'd0) && (sv_ox == 0);
            out_eol <= sv_eol;
            out_eof <= sv_eol && (oy == last_oy);
        end
    end
endmodule
