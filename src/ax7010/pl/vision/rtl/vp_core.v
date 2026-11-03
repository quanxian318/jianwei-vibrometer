// Vision core at the working resolution, bit-exact with vision::core() in
// ax7010/linux/vibmon/src/measure/vision.cpp:
//
//   while the frame arrives: G0 stream -> G0 buffer, REDUCE -> G1, REDUCE -> G2
//   phase B, per band pixel: L1 = G1 - EXPAND(G2); two first-order low-passes
//            (Q6 states), band = lpH - lpL; amp += (|band| - amp) >> 4;
//            delta = clamp((gain * band + 32) >> 6, +-96)
//   phase C, per G0 pixel: magnified = clamp(G0 + EXPAND(delta)), written over G0
//
// G0/G1/G2 are double-buffered: the next frame streams in while phases B and
// C run on the previous one (13 clocks per band pixel, 11 per G0 pixel). A
// frame that completes while B/C are still busy is counted in `dropped`.
// The G0 width must be a multiple of 4 (the stream reducers need even widths).
`timescale 1ns / 1ps
module vp_core #(
    parameter MAXW0 = 200,
    parameter MAXH0 = 150
) (
    input  wire        clk,
    input  wire        rst,
    input  wire [15:0] coef_h,          // Q16 low-pass coefficients (computed by the PS from the frame rate)
    input  wire [15:0] coef_l,
    input  wire [7:0]  gain,
    input  wire [15:0] g0_height,       // rows of the G0 frame
    input  wire        in_valid,
    input  wire [7:0]  in_data,
    input  wire        in_sof,
    input  wire        in_eol,
    output reg         done = 1'b0,     // one clock: the outputs of a frame are complete
    output reg         out_buf = 1'b0,  // G0 buffer that holds the latest magnified frame
    output reg  [15:0] w0 = 0, h0 = 0, w1 = 0, h1 = 0,
    output wire        busy,
    output reg  [31:0] frames = 0,
    output reg  [31:0] dropped = 0,
    // Read ports (PS or testbench), one clock latency.
    input  wire [15:0] rd_mag_addr,
    output reg  [7:0]  rd_mag = 8'd0,
    input  wire [15:0] rd_amp_addr,
    output reg  [15:0] rd_amp = 16'd0
);
    localparam MAXW1 = (MAXW0 + 1) / 2, MAXH1 = (MAXH0 + 1) / 2;
    localparam MAXW2 = (MAXW1 + 1) / 2, MAXH2 = (MAXH1 + 1) / 2;
    localparam N0 = MAXW0 * MAXH0, N1 = MAXW1 * MAXH1, N2 = MAXW2 * MAXH2;

    // ------------------------------------------------------------------
    // Streams: G0 -> REDUCE -> G1 -> REDUCE -> G2 into the write buffers
    // ------------------------------------------------------------------
    reg wbuf = 1'b0;
    wire [15:0] g1_height = (g0_height + 16'd1) >> 1;
    wire v1, s1, e1, v2, s2, e2, f2;
    wire [7:0] d1, d2;
    vp_reduce #(.MAXW(MAXW0)) u_red1 (.clk(clk), .rst(rst), .height(g0_height), .in_valid(in_valid),
        .in_data(in_data), .in_sof(in_sof), .in_eol(in_eol), .out_valid(v1), .out_data(d1), .out_sof(s1),
        .out_eol(e1), .out_eof(), .overrun());
    vp_reduce #(.MAXW(MAXW1)) u_red2 (.clk(clk), .rst(rst), .height(g1_height), .in_valid(v1), .in_data(d1),
        .in_sof(s1), .in_eol(e1), .out_valid(v2), .out_data(d2), .out_sof(s2), .out_eol(e2), .out_eof(f2),
        .overrun());

    reg [15:0] a0 = 0, a1 = 0, a2 = 0;    // write addresses
    reg [15:0] col0 = 0, row0 = 0;        // G0 stream position
    reg [15:0] fw0 = 0;                   // width of the incoming G0 frame
    wire [15:0] wa0 = in_sof ? 16'd0 : a0;
    wire [15:0] wa1 = s1 ? 16'd0 : a1;
    wire [15:0] wa2 = s2 ? 16'd0 : a2;
    wire [15:0] c0 = in_sof ? 16'd0 : col0;
    always @(posedge clk) begin
        if (in_valid) begin
            a0 <= wa0 + 16'd1;
            col0 <= in_eol ? 16'd0 : c0 + 16'd1;
            if (in_eol && (in_sof || row0 == 16'd0)) fw0 <= c0 + 16'd1;  // first row gives the width
            row0 <= in_sof ? {15'd0, in_eol} : row0 + {15'd0, in_eol};
        end
        if (v1) a1 <= wa1 + 16'd1;
        if (v2) a2 <= wa2 + 16'd1;
    end

    // ------------------------------------------------------------------
    // Sequencer state
    // ------------------------------------------------------------------
    localparam P_IDLE = 3'd0, P_BTAP = 3'd1, P_BACC = 3'd2, P_BUPD1 = 3'd3, P_BUPD2 = 3'd4, P_CTAP = 3'd5,
               P_CACC = 3'd6, P_CWR = 3'd7;
    reg [2:0]  ph = P_IDLE;
    reg        pbuf = 1'b0;                 // buffers being processed
    reg [15:0] pw0 = 0, ph0 = 0, pw1 = 0, ph1 = 0, pw2 = 0, ph2 = 0;
    reg [15:0] px = 0, py = 0;              // output coordinate of the current pixel
    reg [15:0] paddr = 0;                   // its linear address
    reg [15:0] waddr = 0;                   // address of the pending write
    reg [3:0]  k = 0;
    reg [1:0]  tx = 0, ty = 0;
    assign busy = (ph != P_IDLE);

    // Tap along one axis for output coordinate c: even c -> i-1, i, i+1
    // (weights 1 6 1), odd c -> i, i+1, - (weights 4 4 0); clamped to 0..n-1.
    function [15:0] tap_idx(input [15:0] c, input [1:0] t, input [15:0] n);
        reg signed [17:0] v;
        begin
            v = $signed({3'b000, c[15:1]});
            if (!c[0]) begin
                if (t == 2'd0) v = v - 18'sd1;
                if (t == 2'd2) v = v + 18'sd1;
            end else if (t != 2'd0) begin
                v = v + 18'sd1;
            end
            if (v < 18'sd0) v = 18'sd0;
            if (v > $signed({2'b00, n}) - 18'sd1) v = $signed({2'b00, n}) - 18'sd1;
            tap_idx = v[15:0];
        end
    endfunction

    function [2:0] tap_w(input c0, input [1:0] t);
        tap_w = !c0 ? ((t == 2'd1) ? 3'd6 : 3'd1) : ((t == 2'd2) ? 3'd0 : 3'd4);
    endfunction

    // Phase B expands G2 (pw2 x ph2) at band coordinates, phase C expands delta (pw1 x ph1).
    wire        in_b = (ph == P_BTAP);
    wire [15:0] sw = in_b ? pw2 : pw1;
    wire [15:0] sh = in_b ? ph2 : ph1;
    wire [15:0] tix = tap_idx(px, tx, sw);
    wire [15:0] tiy = tap_idx(py, ty, sh);
    wire [31:0] tprod = tiy * sw;
    wire [15:0] taddr = tprod[15:0] + tix;
    wire [5:0]  tw = tap_w(px[0], tx) * tap_w(py[0], ty);

    // ------------------------------------------------------------------
    // Memories (synchronous read)
    // ------------------------------------------------------------------
    reg [7:0]  g0a [0:N0-1];
    reg [7:0]  g0b [0:N0-1];
    reg [7:0]  g1a [0:N1-1];
    reg [7:0]  g1b [0:N1-1];
    reg [7:0]  g2a [0:N2-1];
    reg [7:0]  g2b [0:N2-1];
    reg [15:0] lph [0:N1-1];
    reg [15:0] lpl [0:N1-1];
    reg [15:0] amp [0:N1-1];
    reg [7:0]  dlt [0:N1-1];

    reg [7:0]  g0q = 0, g1q = 0, g2q = 0, dltq = 0;
    reg [15:0] lphq = 0, lplq = 0, ampq = 0;
    reg        b_we = 1'b0, c_we = 1'b0;
    reg [15:0] nh = 0, nl = 0, na = 0;
    reg [7:0]  nd = 0, nmag = 0;

    integer i;
    initial begin
        for (i = 0; i < N1; i = i + 1) begin
            lph[i] = 16'd0;
            lpl[i] = 16'd0;
            amp[i] = 16'd0;
        end
    end

    always @(posedge clk) begin  // G0: stream writes wbuf, phase C works in place on pbuf
        if (in_valid && !wbuf) g0a[wa0] <= in_data;
        if (in_valid && wbuf) g0b[wa0] <= in_data;
        if (c_we && !pbuf) g0a[waddr] <= nmag;
        if (c_we && pbuf) g0b[waddr] <= nmag;
        g0q <= pbuf ? g0b[paddr] : g0a[paddr];
        rd_mag <= out_buf ? g0b[rd_mag_addr] : g0a[rd_mag_addr];
    end
    always @(posedge clk) begin
        if (v1 && !wbuf) g1a[wa1] <= d1;
        if (v1 && wbuf) g1b[wa1] <= d1;
        g1q <= pbuf ? g1b[paddr] : g1a[paddr];
    end
    always @(posedge clk) begin
        if (v2 && !wbuf) g2a[wa2] <= d2;
        if (v2 && wbuf) g2b[wa2] <= d2;
        g2q <= pbuf ? g2b[taddr] : g2a[taddr];
    end
    always @(posedge clk) begin
        if (b_we) begin
            lph[waddr] <= nh;
            lpl[waddr] <= nl;
            amp[waddr] <= na;
            dlt[waddr] <= nd;
        end
        lphq <= lph[paddr];
        lplq <= lpl[paddr];
        ampq <= amp[paddr];
        dltq <= dlt[taddr];
        rd_amp <= amp[rd_amp_addr];
    end

    // ------------------------------------------------------------------
    // Arithmetic
    // ------------------------------------------------------------------
    reg signed [19:0] acc = 0;
    reg [5:0]  tw_d = 0;
    reg        tap_d = 1'b0;
    wire signed [8:0] tap_val = (ph == P_CTAP || ph == P_CACC) ? $signed({dltq[7], dltq}) : $signed({1'b0, g2q});
    wire signed [19:0] tap_prod = $signed({1'b0, tw_d}) * tap_val;
    wire signed [19:0] e_val = (acc + 20'sd32) >>> 6;  // EXPAND output
    wire signed [16:0] e17 = e_val[16:0];
    wire signed [16:0] lap = $signed({9'd0, g1q}) - e17;
    wire signed [22:0] x6 = lap * 23'sd64;
    wire signed [22:0] dh = x6 - $signed(lphq);
    wire signed [22:0] dl = x6 - $signed(lplq);
    wire signed [39:0] mh = $signed({1'b0, coef_h}) * dh;
    wire signed [39:0] ml = $signed({1'b0, coef_l}) * dl;
    wire signed [39:0] uh40 = (mh + 40'sd32768) >>> 16;
    wire signed [39:0] ul40 = (ml + 40'sd32768) >>> 16;
    wire signed [23:0] uh = uh40[23:0];  // |update| < 2^22: the low bits keep the value
    wire signed [23:0] ul = ul40[23:0];
    wire signed [23:0] h_new = $signed(lphq) + uh;
    wire signed [23:0] l_new = $signed(lplq) + ul;

    function [15:0] sat16(input signed [23:0] v);
        sat16 = (v > 24'sd32767) ? 16'h7FFF : ((v < -24'sd32768) ? 16'h8000 : v[15:0]);
    endfunction

    reg [15:0] hq = 0, lq = 0;                 // new states (BUPD1 -> BUPD2)
    wire signed [16:0] bdiff = $signed(hq) - $signed(lq);
    wire signed [15:0] band = (bdiff > 17'sd32767) ? 16'sh7FFF : ((bdiff < -17'sd32768) ? 16'sh8000 : bdiff[15:0]);
    wire [16:0] babs = band[15] ? (17'd0 - {band[15], band}) : {1'b0, band};
    wire signed [17:0] adiff = $signed({1'b0, babs}) - $signed({2'b00, ampq});
    wire signed [17:0] a_new = $signed({2'b00, ampq}) + (adiff >>> 4);
    wire signed [24:0] gb = $signed({1'b0, gain}) * band;
    wire signed [24:0] gd = (gb + 25'sd32) >>> 6;
    wire [7:0] d_new = (gd > 25'sd96) ? 8'd96 : ((gd < -25'sd96) ? 8'hA0 : gd[7:0]);  // 8'hA0 = -96
    wire signed [9:0] e10 = e_val[9:0];
    wire signed [9:0] m_sum = $signed({2'b00, g0q}) + e10;
    wire [7:0] m_new = (m_sum < 10'sd0) ? 8'd0 : ((m_sum > 10'sd255) ? 8'd255 : m_sum[7:0]);

    // ------------------------------------------------------------------
    // Sequencer
    // ------------------------------------------------------------------
    wire frame_end = v2 && f2;  // last G2 pixel of the incoming frame
    wire [15:0] nw1 = (fw0 + 16'd1) >> 1, nh1 = (g0_height + 16'd1) >> 1;
    wire last_px = (px + 16'd1 >= ((ph == P_BUPD2) ? pw1 : pw0));
    wire last_py = (py + 16'd1 >= ((ph == P_BUPD2) ? ph1 : ph0));

    always @(posedge clk) begin
        done <= 1'b0;
        b_we <= 1'b0;
        c_we <= 1'b0;
        tap_d <= 1'b0;
        if (rst) begin
            ph <= P_IDLE;
            wbuf <= 1'b0;
        end else begin
            if (tap_d) acc <= acc + tap_prod;
            if (frame_end) begin
                if (ph == P_IDLE) begin
                    pbuf <= wbuf;
                    wbuf <= ~wbuf;
                    pw0 <= fw0;
                    ph0 <= g0_height;
                    pw1 <= nw1;
                    ph1 <= nh1;
                    pw2 <= (nw1 + 16'd1) >> 1;
                    ph2 <= (nh1 + 16'd1) >> 1;
                    w0 <= fw0;
                    h0 <= g0_height;
                    w1 <= nw1;
                    h1 <= nh1;
                    px <= 16'd0;
                    py <= 16'd0;
                    paddr <= 16'd0;
                    k <= 4'd0;
                    tx <= 2'd0;
                    ty <= 2'd0;
                    acc <= 20'sd0;
                    ph <= P_BTAP;
                end else begin
                    dropped <= dropped + 32'd1;
                end
            end
            case (ph)
                P_BTAP, P_CTAP: begin
                    tap_d <= 1'b1;
                    tw_d <= tw;
                    k <= k + 4'd1;
                    if (tx == 2'd2) begin
                        tx <= 2'd0;
                        ty <= ty + 2'd1;
                    end else begin
                        tx <= tx + 2'd1;
                    end
                    if (k == 4'd8) ph <= (ph == P_BTAP) ? P_BACC : P_CACC;
                end
                P_BACC: ph <= P_BUPD1;  // the last tap accumulates on this edge
                P_BUPD1: begin
                    hq <= sat16(h_new);
                    lq <= sat16(l_new);
                    ph <= P_BUPD2;
                end
                P_CACC: ph <= P_CWR;
                P_BUPD2, P_CWR: begin
                    if (ph == P_BUPD2) begin
                        nh <= hq;
                        nl <= lq;
                        na <= a_new[15:0];
                        nd <= d_new;
                        b_we <= 1'b1;
                    end else begin
                        nmag <= m_new;
                        c_we <= 1'b1;
                    end
                    waddr <= paddr;
                    k <= 4'd0;
                    tx <= 2'd0;
                    ty <= 2'd0;
                    acc <= 20'sd0;
                    paddr <= paddr + 16'd1;
                    if (!last_px) begin
                        px <= px + 16'd1;
                        ph <= (ph == P_BUPD2) ? P_BTAP : P_CTAP;
                    end else begin
                        px <= 16'd0;
                        if (!last_py) begin
                            py <= py + 16'd1;
                            ph <= (ph == P_BUPD2) ? P_BTAP : P_CTAP;
                        end else if (ph == P_BUPD2) begin
                            py <= 16'd0;  // band done: magnify the G0 frame
                            paddr <= 16'd0;
                            ph <= P_CTAP;
                        end else begin
                            ph <= P_IDLE;  // frame done
                            done <= 1'b1;
                            out_buf <= pbuf;
                            frames <= frames + 32'd1;
                        end
                    end
                end
                default: ;
            endcase
        end
    end
endmodule
