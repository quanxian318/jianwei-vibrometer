// lk_accum - shared displacement engine pass (route proposal section 4.1).
//
// One pass walks the w x h template once, one pixel per clock:
//   I_w = ((256-ay)*((256-ax)*p00 + ax*p10) + ay*((256-ax)*p01 + ax*p11) + 128) >> 8
//   s1 += I_w;  s2 += I_w*I_w;  sx += gx*I_w;  sy += gy*I_w;  st += t*I_w
// p00..p11 are the window pixels at (ox+x, oy+y) .. (ox+x+1, oy+y+1).
// Bit-exact with lkAccumulate() in linux/vibmon/src/measure/lk_core.cpp.
//
// With ax = ay = 0 a pass gives the ZNCC sums of an integer candidate (integer
// search); with a fractional offset it gives one inverse-compositional LK
// update. The PS (LkTracker) turns the five sums into the score or the 2x2
// update, so the same engine serves both stages and several ROIs in turn.
//
// Memories: the window buffer (filled by roi_capture from the video stream,
// stride = window width) is kept twice so the four bilinear neighbours are
// read in one clock from two dual-port copies; the coefficient memory holds
// {t, gx, gy} (int16 each, Q6) per template pixel, written by the PS when a
// template is taken. Both write ports are plain RAM ports here; the AXI/BRAM
// wrapper is not part of this module.
`timescale 1ns / 1ps
module lk_accum #(
    parameter WIN_BYTES = 4096,    // window buffer size (>= stride * window rows)
    parameter MAXN      = 4096     // template pixels (64 x 64)
) (
    input  wire        clk,
    input  wire        rst,
    // window buffer write port
    input  wire        win_we,
    input  wire [15:0] win_addr,
    input  wire [7:0]  win_data,
    // coefficient write port: {t, gx, gy}
    input  wire        coef_we,
    input  wire [15:0] coef_addr,
    input  wire [47:0] coef_data,
    // pass parameters, sampled on start
    input  wire        start,
    input  wire [7:0]  w,           // template width  (8..64)
    input  wire [7:0]  h,           // template height (8..64)
    input  wire [15:0] stride,      // window buffer row pitch
    input  wire [7:0]  ox,          // integer offset of the pass inside the window
    input  wire [7:0]  oy,
    input  wire [7:0]  ax,          // fractional offset, 1/256 px
    input  wire [7:0]  ay,
    output reg         busy = 1'b0,
    output reg         done = 1'b0, // one-clock pulse, sums valid from then on
    output reg  [31:0] cycles = 32'd0,
    output reg signed [47:0] s1 = 48'sd0,
    output reg signed [47:0] s2 = 48'sd0,
    output reg signed [47:0] sx = 48'sd0,
    output reg signed [47:0] sy = 48'sd0,
    output reg signed [47:0] st = 48'sd0
);
    localparam WA = $clog2(WIN_BYTES);
    localparam CA = $clog2(MAXN);

    reg [7:0]  winA [0:WIN_BYTES-1];  // rows y   : p00, p10
    reg [7:0]  winB [0:WIN_BYTES-1];  // rows y+1 : p01, p11
    reg [47:0] coef [0:MAXN-1];
    always @(posedge clk) begin
        if (win_we) begin
            winA[win_addr[WA-1:0]] <= win_data;
            winB[win_addr[WA-1:0]] <= win_data;
        end
        if (coef_we) coef[coef_addr[CA-1:0]] <= coef_data;
    end

    // ------------------------------------------------------------------
    // S0: pixel counters and addresses
    // ------------------------------------------------------------------
    reg [7:0]  pw = 0, ph = 0, cx = 0, cy = 0;
    reg [15:0] pstride = 0, row_base = 0;
    reg [8:0]  bxw = 0, axw = 0, byw = 0, ayw = 0;  // 256-ax, ax, 256-ay, ay
    reg [CA-1:0] cidx = 0;
    reg        run = 1'b0;
    reg        a_v = 1'b0, a_last = 1'b0;
    reg [WA-1:0] a00 = 0;
    reg [CA-1:0] a_c = 0;
    always @(posedge clk) begin
        a_v <= 1'b0;
        a_last <= 1'b0;
        if (rst) begin
            run <= 1'b0;
        end else if (start && !busy) begin
            pw <= w;
            ph <= h;
            pstride <= stride;
            row_base <= oy * stride + ox;
            cx <= 8'd0;
            cy <= 8'd0;
            cidx <= 0;
            bxw <= 9'd256 - ax;
            axw <= {1'b0, ax};
            byw <= 9'd256 - ay;
            ayw <= {1'b0, ay};
            run <= 1'b1;
        end else if (run) begin
            a_v <= 1'b1;
            a00 <= row_base + cx;
            a_c <= cidx;
            cidx <= cidx + 1'b1;
            if (cx == pw - 8'd1) begin
                cx <= 8'd0;
                row_base <= row_base + pstride;
                if (cy == ph - 8'd1) begin
                    run <= 1'b0;
                    a_last <= 1'b1;
                end
                cy <= cy + 8'd1;
            end else begin
                cx <= cx + 8'd1;
            end
        end
    end

    // ------------------------------------------------------------------
    // S1: memory read
    // ------------------------------------------------------------------
    reg        r_v = 1'b0, r_last = 1'b0;
    reg [7:0]  p00 = 0, p10 = 0, p01 = 0, p11 = 0;
    reg [47:0] r_coef = 0;
    always @(posedge clk) begin
        r_v <= a_v & ~rst;
        r_last <= a_last;
        p00 <= winA[a00];
        p10 <= winA[a00 + 1'b1];
        p01 <= winB[a00 + pstride[WA-1:0]];
        p11 <= winB[a00 + pstride[WA-1:0] + 1'b1];
        r_coef <= coef[a_c];
    end

    // ------------------------------------------------------------------
    // S2: horizontal interpolation (17 bits)
    // ------------------------------------------------------------------
    reg        h_v = 1'b0, h_last = 1'b0;
    reg [16:0] top = 0, bot = 0;
    reg [47:0] h_coef = 0;
    always @(posedge clk) begin
        h_v <= r_v & ~rst;
        h_last <= r_last;
        top <= bxw * p00 + axw * p10;
        bot <= bxw * p01 + axw * p11;
        h_coef <= r_coef;
    end

    // ------------------------------------------------------------------
    // S3: vertical interpolation, round to Q8.8 (0..65280)
    // ------------------------------------------------------------------
    reg        v_v = 1'b0, v_last = 1'b0;
    reg [16:0] iw = 0;
    reg [47:0] v_coef = 0;
    wire [25:0] vsum = byw * top + ayw * bot + 26'd128;
    always @(posedge clk) begin
        v_v <= h_v & ~rst;
        v_last <= h_last;
        iw <= vsum[25:8];
        v_coef <= h_coef;
    end

    // ------------------------------------------------------------------
    // S4: products
    // ------------------------------------------------------------------
    reg        m_v = 1'b0, m_last = 1'b0;
    reg signed [33:0] m_ii = 0, m_x = 0, m_y = 0, m_t = 0;
    reg [16:0] m_i = 0;
    wire signed [17:0] iws = {1'b0, iw};
    wire signed [15:0] ct = v_coef[47:32], cgx = v_coef[31:16], cgy = v_coef[15:0];
    always @(posedge clk) begin
        m_v <= v_v & ~rst;
        m_last <= v_last;
        m_i <= iw;
        m_ii <= iws * iws;
        m_x <= cgx * iws;
        m_y <= cgy * iws;
        m_t <= ct * iws;
    end

    // ------------------------------------------------------------------
    // S5: 48-bit accumulators, control
    // ------------------------------------------------------------------
    always @(posedge clk) begin
        done <= 1'b0;
        if (rst) begin
            busy <= 1'b0;
        end else if (start && !busy) begin
            busy <= 1'b1;
            cycles <= 32'd0;
            s1 <= 48'sd0;
            s2 <= 48'sd0;
            sx <= 48'sd0;
            sy <= 48'sd0;
            st <= 48'sd0;
        end else if (busy) begin
            cycles <= cycles + 32'd1;
            if (m_v) begin
                s1 <= s1 + $signed({31'd0, m_i});
                s2 <= s2 + m_ii;
                sx <= sx + m_x;
                sy <= sy + m_y;
                st <= st + m_t;
                if (m_last) begin
                    busy <= 1'b0;
                    done <= 1'b1;
                end
            end
        end
    end
endmodule
