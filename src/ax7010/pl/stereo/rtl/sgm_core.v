// sgm_core - stereo matching core: Hamming cost, 2-path semi-global
// aggregation (left->right and top->down), winner-take-all, uniqueness,
// equiangular sub-pixel fit and left-right consistency check.
// Bit-exact with sgmRaw() in linux/vibmon/src/stereo/stereo_match.cpp for
// census = 9 (center-symmetric 9x7, 31 bit), paths = 2, lrCheck = true.
//
// The PS rectifies both images and computes the census descriptors; this core
// takes one pixel {cl, cr, gray} per period in raster order (all pixels,
// borders included) and returns the left disparity in 1/16 px (-1 invalid)
// row by row. Texture gate, LK refinement, median and speckle filters stay
// on the PS (stereoPostProcess).
//
// One disparity per clock: a pixel period is D + 2 clocks, so 320 x 180 x 64
// takes about 3.8 M clocks (38 ms at 100 MHz). Storage: one row of top-path
// costs (MAXW x D x 7 bit, block RAM), a few MAXW-entry rows and small
// per-pixel buffers (distributed RAM).
//
// Pipeline per pixel: sweep (path costs, S = Lh + Lt, right-disparity
// minimum per right pixel) -> A (best d) -> B (second best not adjacent,
// neighbours of the best) -> C (uniqueness, sub-pixel, store). When a row's
// last pixel leaves C, the LR engine emits that row (and clears its buffers).
`timescale 1ns / 1ps
module sgm_core #(
    parameter D      = 64,   // disparities 0..D-1 (power of two)
    parameter LD     = 6,    // log2(D)
    parameter BITS   = 31,   // census length
    parameter BORDER = 4,    // census border (9x7 window)
    parameter P1     = 7,
    parameter P2     = 86,
    parameter UNIQ   = 5,    // percent
    parameter LRTOL  = 1,
    parameter MAXW   = 320,
    parameter XW     = 10    // bits of x / width
) (
    input  wire          clk,
    input  wire          rst,
    input  wire          start,          // begin a frame (w, h sampled)
    input  wire [XW-1:0] w,
    input  wire [XW-1:0] h,
    input  wire          in_valid,
    output wire          in_ready,
    input  wire [30:0]   in_cl,
    input  wire [30:0]   in_cr,
    input  wire [7:0]    in_gray,
    output reg           out_valid = 1'b0,
    output reg  [15:0]   out_q4 = 16'd0,
    output reg  [XW-1:0] out_x = 0,
    output reg  [XW-1:0] out_y = 0,
    output reg           frame_done = 1'b0,
    output wire          busy
);
    localparam LW = 7;   // path cost width (<= BITS + P2 = 117)
    localparam SWD = 8;  // S = Lh + Lt <= 234
    localparam [1:0] S_IDLE = 2'd0, S_CLEAR = 2'd1, S_RUN = 2'd2, S_FLUSH = 2'd3;

    // ------------------------------------------------------------------
    // Adaptive P2 table: max(P1 + 1, P2 * 16 / (16 + |dI|))
    // ------------------------------------------------------------------
    reg [LW-1:0] p2rom [0:255];
    integer ii;
    initial begin
        for (ii = 0; ii < 256; ii = ii + 1)
            p2rom[ii] = ((P2 * 16) / (16 + ii) > P1 + 1) ? (P2 * 16) / (16 + ii) : P1 + 1;
    end

    function [5:0] popcount31(input [30:0] v);
        integer k;
        begin
            popcount31 = 0;
            for (k = 0; k < 31; k = k + 1) popcount31 = popcount31 + v[k];
        end
    endfunction

    // ------------------------------------------------------------------
    // Memories
    // ------------------------------------------------------------------
    reg [LW-1:0]  rowT  [0:MAXW*D-1];   // top-path costs of the previous row (block RAM)
    reg [LW-1:0]  minT  [0:MAXW-1];
    reg [7:0]     gRow  [0:MAXW-1];     // gray of the previous row
    reg [LW-1:0]  hA [0:D-1], hB [0:D-1];  // left->right path, ping-pong per pixel
    reg [SWD-1:0] sb0 [0:D-1], sb1 [0:D-1], sb2 [0:D-1];  // S of the last three pixels
    reg [15:0]    dl0 [0:MAXW-1], dl1 [0:MAXW-1];          // left disparity per row parity
    reg [SWD+LD:0] br0 [0:MAXW-1], br1 [0:MAXW-1];         // {valid, d, cost} right minimum per row parity
    reg [30:0]    crs [0:D-1];          // right census ring: entry (x mod D) holds pixel x of this row

    // ------------------------------------------------------------------
    // Frame control and pixel period
    // ------------------------------------------------------------------
    reg [1:0]    state = S_IDLE;
    reg [XW-1:0] fw = 0, fh = 0, clr = 0;
    reg [XW-1:0] nx = 0, ny = 0;        // next input pixel
    reg          last_in = 1'b0;        // all pixels of the frame accepted
    reg [LD+1:0] ph = 0;                // phase in the period, 0..D+1
    reg          in_period = 1'b0;
    reg          sweep = 1'b0;          // the current period carries a pixel
    reg          prev_sweep = 1'b0;     // the period that just ended carried one
    reg [1:0]    bank = 0;              // S buffer written by the sweep
    reg          hbank = 1'b0;

    assign in_ready = (state == S_RUN) && !in_period && !last_in;
    wire period_go = !in_period && ((state == S_RUN && in_valid && !last_in) || state == S_FLUSH);

    // Current pixel of the sweep.
    reg [30:0]   cl_r = 0;
    reg [7:0]    g_r = 0;
    reg [XW-1:0] x_r = 0, y_r = 0;
    reg [LW-1:0] p2h = 0, p2t = 0, minH = 0, minTp = 0, minHc = 0, minTc = 0;
    reg          wta_r = 1'b0;
    reg [LD-1:0] dmax_r = 0;

    // Stage registers (has = slot carries a pixel).
    reg          a_has = 0, b_has = 0, c_has = 0;
    reg [XW-1:0] a_x = 0, a_y = 0, b_x = 0, b_y = 0, c_x = 0, c_y = 0;
    reg          a_wta = 0, b_wta = 0, c_wta = 0;
    reg [LD-1:0] a_dmax = 0, b_dmax = 0, c_dmax = 0;
    reg [1:0]    a_bank = 0, b_bank = 0;
    reg [LD-1:0] a_best = 0, b_best = 0, c_best = 0;
    reg [SWD-1:0] a_bv = 0;
    reg [SWD-1:0] b_sec = 0, b_cm = 0, b_c0 = 0, b_cp = 0;
    reg          b_hsec = 1'b0;
    reg [SWD-1:0] c_sec = 0, c_cm = 0, c_c0 = 0, c_cp = 0;
    reg          c_hsec = 1'b0;

    // LR engine
    reg [1:0]    lr_state = 2'd0;       // 0 idle, 1 output, 2 clear
    reg [XW-1:0] lr_x = 0, lr_y = 0;
    reg          lr_bank = 1'b0;
    reg          lr_last = 1'b0;        // this row is the frame's last

    assign busy = state != S_IDLE;

    // ------------------------------------------------------------------
    // Sweep datapath (one disparity per clock, d = ph - 2)
    // ------------------------------------------------------------------
    reg  [LW-1:0] t_rd = 0, tw0 = 0, tw1 = 0;   // top-path window: d-1, d (t_rd = d+1)
    wire [LD+1:0] dd = ph - 2;
    wire [LD-1:0] d = dd[LD-1:0];
    wire          comp = sweep && in_period && ph >= 2;
    wire [LW-1:0] hp_m1 = hbank ? hB[d - 1'b1] : hA[d - 1'b1];
    wire [LW-1:0] hp_0  = hbank ? hB[d] : hA[d];
    wire [LW-1:0] hp_p1 = hbank ? hB[d + 1'b1] : hA[d + 1'b1];
    wire          d_first = (d == 0), d_last = (d == D - 1);
    wire [LD-1:0] cr_idx = x_r[LD-1:0] - d;
    wire [5:0]    cost = ({{(XW-LD){1'b0}}, d} <= x_r) ? popcount31(cl_r ^ crs[cr_idx]) : BITS[5:0];

    function [LW-1:0] path;
        input [5:0] c;
        input [LW-1:0] m1, z0, p1, mp, p2v;
        input first, last, start_path;
        reg [8:0] v;
        begin
            if (start_path) path = c;
            else begin
                v = {2'b0, z0};
                if ({1'b0, mp} + p2v < v) v = {1'b0, mp} + p2v;
                if (!first && {2'b0, m1} + P1 < v) v = {2'b0, m1} + P1;
                if (!last && {2'b0, p1} + P1 < v) v = {2'b0, p1} + P1;
                path = c + v - mp;
            end
        end
    endfunction

    wire [LW-1:0] lh = path(cost, hp_m1, hp_0, hp_p1, minH, p2h, d_first, d_last, x_r == 0);
    wire [LW-1:0] lt = path(cost, tw0, tw1, t_rd, minTp, p2t, d_first, d_last, y_r == 0);
    wire [SWD-1:0] s_now = lh + lt;
    wire [XW-1:0] xr_up = x_r - d;
    // LR engine (row of one parity) and sweep (the other) share each bank's
    // single read port; they never use the same bank at the same time.
    wire [15:0]   lr_q = lr_bank ? dl1[lr_x] : dl0[lr_x];
    wire [LD:0]   lr_dq = (lr_q + 16'd8) >> 4;
    wire [XW:0]   lr_xr = {1'b0, lr_x} - lr_dq;
    wire          lr_rd = lr_state == 2'd1;
    wire [XW-1:0] br_ra0 = (lr_rd && !lr_bank) ? lr_xr[XW-1:0] : xr_up;
    wire [XW-1:0] br_ra1 = (lr_rd && lr_bank) ? lr_xr[XW-1:0] : xr_up;
    wire [SWD+LD:0] br_rd0 = br0[br_ra0], br_rd1 = br1[br_ra1];
    wire [SWD+LD:0] br_old = y_r[0] ? br_rd1 : br_rd0;
    wire          br_upd = comp && wta_r && d <= dmax_r && (!br_old[SWD+LD] || s_now < br_old[SWD-1:0]);
    wire [LW-1:0] minH_n = (d_first || lh < minHc) ? lh : minHc;
    wire [LW-1:0] minT_n = (d_first || lt < minTc) ? lt : minTc;

    // ------------------------------------------------------------------
    // Stage C helpers
    // ------------------------------------------------------------------
    reg          c_ok = 1'b0;
    reg signed [15:0] c_nn = 0;
    reg [15:0]   c_mag = 0, c_den = 0;
    reg [3:0]    c_q = 0;
    reg          c_div = 1'b0;
    wire [SWD-1:0] c_mx = (c_cm > c_cp) ? c_cm : c_cp;
    wire [15:0]  den_w = {7'd0, c_mx - c_c0, 1'b0};
    wire signed [15:0] num_w = $signed({4'd0, c_cm, 4'd0}) - $signed({4'd0, c_cp, 4'd0});
    wire signed [15:0] nn_w = (num_w >= 0) ? num_w + $signed({1'b0, den_w[15:1]}) : num_w - $signed({1'b0, den_w[15:1]});
    wire [3:0]   q_cl = (c_q > 8) ? 4'd8 : c_q;
    wire signed [15:0] off_w = c_nn[15] ? -$signed({12'd0, q_cl}) : $signed({12'd0, q_cl});
    wire [15:0]  q4_sub = {c_best, 4'd0} + off_w;

    integer k;
    always @(posedge clk) begin
        out_valid <= 1'b0;
        frame_done <= 1'b0;
        if (rst) begin
            state <= S_IDLE;
            in_period <= 1'b0;
            lr_state <= 2'd0;
            a_has <= 1'b0; b_has <= 1'b0; c_has <= 1'b0;
        end else begin
            // ---------------- frame control ----------------
            case (state)
                S_IDLE: if (start) begin
                    fw <= w; fh <= h; clr <= 0; state <= S_CLEAR;
                    nx <= 0; ny <= 0; last_in <= 1'b0; bank <= 0; hbank <= 1'b0; prev_sweep <= 1'b0;
                end
                S_CLEAR: begin
                    clr <= clr + 1'b1;
                    if (clr == fw - 1'b1) state <= S_RUN;
                end
                S_RUN: if (last_in && !in_period) state <= S_FLUSH;
                S_FLUSH: if (!prev_sweep && !a_has && !b_has && !c_has && lr_state == 2'd0 && !in_period && !sweep) begin
                    state <= S_IDLE;
                    frame_done <= 1'b1;
                end
                default: state <= S_IDLE;
            endcase

            // ---------------- period start ----------------
            if (period_go) begin
                in_period <= 1'b1;
                ph <= 0;
                sweep <= (state == S_RUN);
                if (state == S_RUN) begin
                    cl_r <= in_cl;
                    g_r <= in_gray;
                    x_r <= nx;
                    y_r <= ny;
                    crs[nx[LD-1:0]] <= in_cr;
                    p2h <= p2rom[(in_gray > g_r) ? in_gray - g_r : g_r - in_gray];
                    p2t <= p2rom[(in_gray > gRow[nx]) ? in_gray - gRow[nx] : gRow[nx] - in_gray];
                    minTp <= minT[nx];
                    gRow[nx] <= in_gray;
                    wta_r <= ny >= BORDER && ny < fh - BORDER && nx >= BORDER && nx < fw - BORDER;
                    dmax_r <= (nx - BORDER > D - 1) ? D - 1 : nx - BORDER;
                    if (nx == fw - 1'b1) begin
                        nx <= 0;
                        ny <= ny + 1'b1;
                        if (ny == fh - 1'b1) last_in <= 1'b1;
                    end else nx <= nx + 1'b1;
                end
                // advance the stage slots
                a_has <= prev_sweep; a_x <= x_r; a_y <= y_r; a_wta <= wta_r; a_dmax <= dmax_r; a_bank <= bank;
                b_has <= a_has; b_x <= a_x; b_y <= a_y; b_wta <= a_wta; b_dmax <= a_dmax; b_bank <= a_bank; b_best <= a_best;
                c_has <= b_has; c_x <= b_x; c_y <= b_y; c_wta <= b_wta; c_dmax <= b_dmax; c_best <= b_best;
                c_sec <= b_sec; c_hsec <= b_hsec; c_cm <= b_cm; c_c0 <= b_c0; c_cp <= b_cp;
                if (prev_sweep) begin
                    bank <= (bank == 2'd2) ? 2'd0 : bank + 1'b1;
                    hbank <= ~hbank;
                end
            end else if (in_period) begin
                ph <= ph + 1'b1;
                if (ph == D + 1) begin
                    in_period <= 1'b0;
                    prev_sweep <= sweep;
                    sweep <= 1'b0;
                end
            end

            // ---------------- sweep ----------------
            if (sweep && in_period) begin
                // top-path read (index ph) and window
                if (ph < D) t_rd <= rowT[{x_r[XW-1:0], ph[LD-1:0]}];
                if (ph >= 1) begin
                    tw0 <= tw1;
                    tw1 <= t_rd;
                end
                if (comp) begin
                    if (hbank) hA[d] <= lh; else hB[d] <= lh;
                    rowT[{x_r[XW-1:0], d}] <= lt;
                    case (bank)
                        2'd0: sb0[d] <= s_now;
                        2'd1: sb1[d] <= s_now;
                        default: sb2[d] <= s_now;
                    endcase
                    minHc <= minH_n;
                    minTc <= minT_n;
                    if (d_last) begin
                        minH <= minH_n;
                        minT[x_r] <= minT_n;
                    end
                end
            end

            // ---------------- stage A: best ----------------
            if (in_period && a_has && ph < D) begin : stA
                reg [SWD-1:0] sv;
                sv = (a_bank == 2'd0) ? sb0[ph[LD-1:0]] : (a_bank == 2'd1) ? sb1[ph[LD-1:0]] : sb2[ph[LD-1:0]];
                if (ph[LD-1:0] <= a_dmax && (ph == 0 || sv < a_bv)) begin
                    a_bv <= sv;
                    a_best <= ph[LD-1:0];
                end
            end
            // ---------------- stage B: second best and neighbours ----------------
            if (in_period && b_has && ph < D) begin : stB
                reg [SWD-1:0] sv;
                reg [LD-1:0] kk;
                kk = ph[LD-1:0];
                sv = (b_bank == 2'd0) ? sb0[kk] : (b_bank == 2'd1) ? sb1[kk] : sb2[kk];
                if (ph == 0) b_hsec <= 1'b0;
                if (kk <= b_dmax) begin
                    if ((kk + 1 < b_best || kk > b_best + 1) && (!b_hsec || ph == 0 || sv < b_sec)) begin
                        b_sec <= sv;
                        b_hsec <= 1'b1;
                    end
                    if (kk + 1 == b_best) b_cm <= sv;
                    if (kk == b_best) b_c0 <= sv;
                    if (kk == b_best + 1) b_cp <= sv;
                end
            end
            // ---------------- stage C: uniqueness, sub-pixel, store ----------------
            if (in_period && c_has) begin
                if (ph == 0) begin
                    c_ok <= c_wta && c_hsec && (c_c0 * 100 < c_sec * (100 - UNIQ));
                    c_div <= 1'b0;
                    c_q <= 0;
                end else if (ph == 1) begin
                    c_nn <= nn_w;
                    c_mag <= nn_w[15] ? -nn_w : nn_w;
                    c_den <= den_w;
                    c_div <= c_ok && c_best != 0 && c_best < c_dmax && den_w != 0;
                end else if (ph < 12) begin
                    if (c_div && c_mag >= c_den && c_q < 9) begin
                        c_mag <= c_mag - c_den;
                        c_q <= c_q + 1'b1;
                    end
                end else if (ph == 12) begin
                    if (c_x == fw - 1'b1) begin
                        lr_state <= 2'd1;
                        lr_x <= 0;
                        lr_y <= c_y;
                        lr_bank <= c_y[0];
                    end
                end
            end

            // ---------------- LR check and row output ----------------
            if (lr_state == 2'd1) begin : lrc
                reg [SWD+LD:0] e;
                reg ok;
                e = lr_bank ? br_rd1 : br_rd0;
                ok = !lr_q[15] && !lr_xr[XW] && e[SWD+LD] &&
                     ((e[SWD+LD-1:SWD] > lr_dq) ? e[SWD+LD-1:SWD] - lr_dq : lr_dq - e[SWD+LD-1:SWD]) <= LRTOL;
                out_valid <= 1'b1;
                out_q4 <= ok ? lr_q : 16'hffff;
                out_x <= lr_x;
                out_y <= lr_y;
                if (lr_x == fw - 1'b1) begin
                    lr_state <= 2'd2;
                    lr_x <= 0;
                end else lr_x <= lr_x + 1'b1;
            end else if (lr_state == 2'd2) begin
                if (lr_x == fw - 1'b1) lr_state <= 2'd0;
                else lr_x <= lr_x + 1'b1;
            end
        end
    end

    // ------------------------------------------------------------------
    // Row buffers: one write port per bank. Writers of a bank are disjoint
    // in time (frame clear; sweep / stage C of its row; LR clear two rows later).
    // ------------------------------------------------------------------
    wire clr_all = state == S_CLEAR;
    wire lr_clr = lr_state == 2'd2;
    wire c_wr = !rst && in_period && c_has && ph == 12 && c_ok;
    wire [15:0] c_val = c_div ? q4_sub : {c_best, 4'd0};
    wire dl_we0 = clr_all || (lr_clr && !lr_bank) || (c_wr && !c_y[0]);
    wire dl_we1 = clr_all || (lr_clr && lr_bank) || (c_wr && c_y[0]);
    wire [XW-1:0] dl_wa0 = clr_all ? clr : (lr_clr && !lr_bank) ? lr_x : c_x;
    wire [XW-1:0] dl_wa1 = clr_all ? clr : (lr_clr && lr_bank) ? lr_x : c_x;
    wire [15:0] dl_wd0 = (clr_all || (lr_clr && !lr_bank)) ? 16'hffff : c_val;
    wire [15:0] dl_wd1 = (clr_all || (lr_clr && lr_bank)) ? 16'hffff : c_val;
    wire br_we0 = clr_all || (lr_clr && !lr_bank) || (br_upd && !y_r[0]);
    wire br_we1 = clr_all || (lr_clr && lr_bank) || (br_upd && y_r[0]);
    wire [XW-1:0] br_wa0 = clr_all ? clr : (lr_clr && !lr_bank) ? lr_x : xr_up;
    wire [XW-1:0] br_wa1 = clr_all ? clr : (lr_clr && lr_bank) ? lr_x : xr_up;
    wire [SWD+LD:0] br_wd0 = (clr_all || (lr_clr && !lr_bank)) ? 0 : {1'b1, d, s_now};
    wire [SWD+LD:0] br_wd1 = (clr_all || (lr_clr && lr_bank)) ? 0 : {1'b1, d, s_now};
    always @(posedge clk) begin
        if (dl_we0) dl0[dl_wa0] <= dl_wd0;
        if (dl_we1) dl1[dl_wa1] <= dl_wd1;
        if (br_we0) br0[br_wa0] <= br_wd0;
        if (br_we1) br1[br_wa1] <= br_wd1;
    end
endmodule
