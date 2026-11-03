// stereo_core - complete PL stereo matcher: rectified left/right gray in,
// left disparity (1/16 px, 0xFFFF invalid) out, both in raster order.
//   census_cs97 (9x7 center-symmetric census, line buffers)
//   -> sgm_core (Hamming cost, 2-path SGM, WTA, uniqueness, sub-pixel, LR check)
// Stream ports are AXI4-Stream style (valid/ready), meant for an AXI DMA
// MM2S channel ({left, right} bytes per pixel) and an S2MM channel (16 bit
// per pixel) in the vendor block design; that wrapper is not done yet.
// The PS rectifies the camera images (StereoService) and post-processes the
// result (texture gate, hole fill, LK refinement, median, speckle).
`timescale 1ns / 1ps
module stereo_core #(
    parameter D      = 64,
    parameter LD     = 6,
    parameter P1     = 7,
    parameter P2     = 86,
    parameter UNIQ   = 5,
    parameter LRTOL  = 1,
    parameter MAXW   = 320,
    parameter XW     = 10
) (
    input  wire          clk,
    input  wire          rst,
    input  wire          start,
    input  wire [XW-1:0] w,
    input  wire [XW-1:0] h,
    input  wire          s_valid,
    output wire          s_ready,
    input  wire [15:0]   s_data,      // {left gray, right gray}
    output wire          m_valid,
    output wire [15:0]   m_data,      // disparity Q4, 0xFFFF invalid
    output wire          m_last,      // last pixel of a row
    output wire          frame_done,
    output wire          busy
);
    wire          c_valid, c_ready;
    wire [30:0]   cl, cr;
    wire [7:0]    g;
    wire [XW-1:0] ox, oy;
    reg  [XW-1:0] fw = 0;
    always @(posedge clk) if (start) fw <= w;

    census_cs97 #(.MAXW(MAXW), .XW(XW)) u_census (
        .clk(clk), .rst(rst), .start(start), .w(w), .h(h),
        .in_valid(s_valid), .in_ready(s_ready), .in_l(s_data[15:8]), .in_r(s_data[7:0]),
        .out_valid(c_valid), .out_ready(c_ready), .out_cl(cl), .out_cr(cr), .out_gray(g));

    sgm_core #(.D(D), .LD(LD), .BITS(31), .BORDER(4), .P1(P1), .P2(P2), .UNIQ(UNIQ), .LRTOL(LRTOL),
               .MAXW(MAXW), .XW(XW)) u_sgm (
        .clk(clk), .rst(rst), .start(start), .w(w), .h(h),
        .in_valid(c_valid), .in_ready(c_ready), .in_cl(cl), .in_cr(cr), .in_gray(g),
        .out_valid(m_valid), .out_q4(m_data), .out_x(ox), .out_y(oy),
        .frame_done(frame_done), .busy(busy));

    assign m_last = m_valid && ox == fw - 1'b1;
endmodule
