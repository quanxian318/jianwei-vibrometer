// census_cs97 - streaming center-symmetric 9x7 census of a left/right pair.
// Bit-exact with censusTransform(mode 9) in linux/vibmon/src/stereo/stereo_match.cpp:
// 31 bits, MSB first over dy = -3..0, dx = -4..4 (stopping at the centre),
// bit = I(x+dx, y+dy) > I(x-dx, y-dy); 0 within 4 columns / 3 rows of the border.
//
// One output per input with a fixed delay of 3 rows + 4 pixels (six row
// buffers and a 9 x 7 window per image); after the frame's w*h pixels the
// module feeds itself 3w+4 zeros so every pixel is emitted. The centre of the
// window is the input delayed by 3w+4, so the left gray value travels with
// its census (sgm_core needs it for the adaptive P2).
`timescale 1ns / 1ps
module census_cs97 #(
    parameter MAXW = 320,
    parameter XW   = 10
) (
    input  wire          clk,
    input  wire          rst,
    input  wire          start,
    input  wire [XW-1:0] w,
    input  wire [XW-1:0] h,
    input  wire          in_valid,
    output wire          in_ready,
    input  wire [7:0]    in_l,
    input  wire [7:0]    in_r,
    output wire          out_valid,
    input  wire          out_ready,
    output wire [30:0]   out_cl,
    output wire [30:0]   out_cr,
    output wire [7:0]    out_gray
);
    reg [15:0] lb1 [0:MAXW-1], lb2 [0:MAXW-1], lb3 [0:MAXW-1], lb4 [0:MAXW-1], lb5 [0:MAXW-1], lb6 [0:MAXW-1];
    reg [7:0]  wl [0:62];   // 7 rows (y-3..y+3) x 9 columns (x-4..x+4), index row*9 + col
    reg [7:0]  wr [0:62];

    reg [XW-1:0] fw = 0, fh = 0, xi = 0, xo = 0, yo = 0;
    reg [31:0]   n_in = 0, n_total = 0, lag = 0;
    reg          active = 1'b0, pend = 1'b0, pend0 = 1'b0;  // pend0: window updated, census next clock
    reg [30:0]   cl_r = 0, cr_r = 0;
    reg [7:0]    g_r = 0;

    wire feeding = active && n_in < n_total + lag;
    wire real_in = n_in < n_total;
    assign in_ready = feeding && real_in && !pend0 && (!pend || out_ready);
    wire take = feeding && !pend0 && (!pend || out_ready) && (!real_in || in_valid);
    wire [7:0] pl = real_in ? in_l : 8'd0, pr = real_in ? in_r : 8'd0;

    function [30:0] cs;
        input integer sel;  // 0 left, 1 right
        integer dy, dx;
        reg [30:0] v;
        reg [7:0] a, b;
        begin
            v = 0;
            for (dy = -3; dy <= 0; dy = dy + 1)
                for (dx = -4; dx <= 4; dx = dx + 1)
                    if (!(dy == 0 && dx >= 0)) begin
                        a = sel ? wr[(dy + 3) * 9 + dx + 4] : wl[(dy + 3) * 9 + dx + 4];
                        b = sel ? wr[(3 - dy) * 9 + 4 - dx] : wl[(3 - dy) * 9 + 4 - dx];
                        v = {v[29:0], a > b};
                    end
            cs = v;
        end
    endfunction

    wire inner = xo >= 4 && xo < fw - 4 && yo >= 3 && yo < fh - 3;
    assign out_valid = pend;
    assign out_cl = cl_r;
    assign out_cr = cr_r;
    assign out_gray = g_r;

    integer r, c;
    always @(posedge clk) begin
        if (rst) begin
            active <= 1'b0;
            pend <= 1'b0;
            pend0 <= 1'b0;
        end else begin
            // Census of the window updated by the previous take (registered:
            // a function of module arrays must be evaluated procedurally).
            if (pend0) begin
                cl_r <= inner ? cs(0) : 31'd0;
                cr_r <= inner ? cs(1) : 31'd0;
                g_r <= wl[31];
                pend <= 1'b1;
                pend0 <= 1'b0;
            end
            if (start) begin
                fw <= w; fh <= h; xi <= 0; xo <= 0; yo <= 0;
                n_in <= 0; n_total <= w * h; lag <= 3 * w + 4;
                active <= 1'b1;
                pend <= 1'b0;
                pend0 <= 1'b0;
            end else begin
                if (pend && out_ready && !pend0) begin
                    pend <= 1'b0;
                    if (xo == fw - 1'b1) begin
                        xo <= 0;
                        yo <= yo + 1'b1;
                    end else xo <= xo + 1'b1;
                end
                if (take) begin
                    // shift the windows, new column = rows y-6 .. y of column xi
                    for (r = 0; r < 7; r = r + 1)
                        for (c = 0; c < 8; c = c + 1) begin
                            wl[r * 9 + c] <= wl[r * 9 + c + 1];
                            wr[r * 9 + c] <= wr[r * 9 + c + 1];
                        end
                    wl[8] <= lb6[xi][15:8];  wr[8] <= lb6[xi][7:0];
                    wl[17] <= lb5[xi][15:8]; wr[17] <= lb5[xi][7:0];
                    wl[26] <= lb4[xi][15:8]; wr[26] <= lb4[xi][7:0];
                    wl[35] <= lb3[xi][15:8]; wr[35] <= lb3[xi][7:0];
                    wl[44] <= lb2[xi][15:8]; wr[44] <= lb2[xi][7:0];
                    wl[53] <= lb1[xi][15:8]; wr[53] <= lb1[xi][7:0];
                    wl[62] <= pl;
                    wr[62] <= pr;
                    lb6[xi] <= lb5[xi];
                    lb5[xi] <= lb4[xi];
                    lb4[xi] <= lb3[xi];
                    lb3[xi] <= lb2[xi];
                    lb2[xi] <= lb1[xi];
                    lb1[xi] <= {pl, pr};
                    xi <= (xi == fw - 1'b1) ? 0 : xi + 1'b1;
                    n_in <= n_in + 1;
                    if (n_in >= lag) pend0 <= 1'b1;
                    if (n_in + 1 == n_total + lag) active <= 1'b0;
                end
            end
        end
    end
endmodule
