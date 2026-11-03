// stereo_axi - stereo_core for the block design: AXI4-Lite registers,
// AXI4-Stream input from an AXI DMA MM2S channel and AXI4-Stream output to
// the S2MM channel (added by ../../bd/add_stereo.tcl).
//
// Input stream : one 16-bit beat per pixel, tdata = {left gray, right gray},
//                raster order, w*h beats (tlast ignored). In memory (little
//                endian) byte 2i = right, byte 2i+1 = left.
// Output stream: one 16-bit beat per pixel, left disparity in 1/16 px,
//                0xFFFF = invalid; tlast on the frame's last pixel.
// The core emits a row in a burst of w beats without back-pressure, one row
// every w * 67 clocks; a 512-entry FIFO (more than one row) absorbs S2MM stalls. If it ever overflows, STATUS.OVERFLOW
// marks the frame as unusable (the driver discards it).
//
// Register map (mirrored in linux/vibmon/src/drv/stereo_regs.h; a unit test
// compares the two files):
//   0x000 ID       ro  0x53544552 ("STER")
//   0x004 VERSION  ro  0x00010000
//   0x008 CONFIG   ro  [7:0] D, [15:8] P1, [23:16] P2, [27:24] UNIQ %, [31:28] LR tolerance
//   0x00C MAXW     ro  largest supported width
//   0x010 WIDTH    rw  frame width  (16..MAXW)
//   0x014 HEIGHT   rw  frame height (16..1023)
//   0x018 CTRL     wo  bit0 START (begins a frame), bit1 SOFT_RESET
//   0x01C STATUS   r/w1c bit0 BUSY (ro), bit1 DONE (w1c), bit2 OVERFLOW (w1c), bit3 BAD_SIZE (w1c)
//   0x020 IRQ_EN   rw  bit0: interrupt on DONE
//   0x024 CYCLES   ro  clocks from START to DONE of the last frame
//   0x028 FRAMES   ro  completed frames
`timescale 1ns / 1ps
module stereo_axi #(
    parameter MAXW = 320,
    parameter XW   = 10
) (
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi:s_axis:m_axis, ASSOCIATED_RESET s_axi_aresetn" *)
    input  wire        s_axi_aclk,
    input  wire        s_axi_aresetn,
    input  wire [11:0] s_axi_awaddr,
    input  wire [2:0]  s_axi_awprot,
    input  wire        s_axi_awvalid,
    output wire        s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output wire        s_axi_wready,
    output wire [1:0]  s_axi_bresp,
    output reg         s_axi_bvalid = 1'b0,
    input  wire        s_axi_bready,
    input  wire [11:0] s_axi_araddr,
    input  wire [2:0]  s_axi_arprot,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output reg  [31:0] s_axi_rdata = 32'd0,
    output wire [1:0]  s_axi_rresp,
    output reg         s_axi_rvalid = 1'b0,
    input  wire        s_axi_rready,
    // AXI4-Stream from DMA MM2S
    input  wire [15:0] s_axis_tdata,
    input  wire        s_axis_tvalid,
    output wire        s_axis_tready,
    input  wire        s_axis_tlast,
    // AXI4-Stream to DMA S2MM
    output wire [15:0] m_axis_tdata,
    output wire        m_axis_tvalid,
    input  wire        m_axis_tready,
    output wire        m_axis_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:signal:interrupt:1.0 irq INTERRUPT" *)
    (* X_INTERFACE_PARAMETER = "SENSITIVITY LEVEL_HIGH" *)
    output wire        irq
);
    localparam D = 64, P1 = 7, P2 = 86, UNIQ = 5, LRTOL = 1;
    localparam [11:0] REG_ID = 12'h000, REG_VERSION = 12'h004, REG_CONFIG = 12'h008, REG_MAXW = 12'h00C,
                      REG_WIDTH = 12'h010, REG_HEIGHT = 12'h014, REG_CTRL = 12'h018, REG_STATUS = 12'h01C,
                      REG_IRQ_EN = 12'h020, REG_CYCLES = 12'h024, REG_FRAMES = 12'h028;
    localparam [31:0] ID_VALUE = 32'h53544552, VERSION_VALUE = 32'h00010000;

    wire clk = s_axi_aclk;
    reg  soft_rst = 1'b0;
    wire rst = ~s_axi_aresetn | soft_rst;

    reg [XW-1:0] width = 10'd320, height = 10'd180;
    reg          irq_en = 1'b0, done_st = 1'b0, ovf_st = 1'b0, bad_st = 1'b0;
    reg [31:0]   cycles = 0, cyc_run = 0, frames = 0;
    reg          start_p = 1'b0, running = 1'b0;

    // ------------------------------------------------------------------
    // Core
    // ------------------------------------------------------------------
    wire        c_valid, c_last, c_done, c_busy;
    wire [15:0] c_data;
    stereo_core #(.D(D), .LD(6), .P1(P1), .P2(P2), .UNIQ(UNIQ), .LRTOL(LRTOL), .MAXW(MAXW), .XW(XW)) u_core (
        .clk(clk), .rst(rst), .start(start_p), .w(width), .h(height),
        .s_valid(s_axis_tvalid), .s_ready(s_axis_tready), .s_data(s_axis_tdata),
        .m_valid(c_valid), .m_data(c_data), .m_last(c_last), .frame_done(c_done), .busy(c_busy));

    // Output FIFO (512 x {last, data}); tlast only on the frame's last pixel.
    reg [16:0] fifo [0:511];
    reg [9:0]  wp = 0, rp = 0;
    reg [XW-1:0] orow = 0;
    wire fifo_empty = wp == rp;
    wire fifo_full = (wp - rp) == 10'd512;
    wire frame_last = c_last && orow == height - 1'b1;
    assign m_axis_tvalid = !fifo_empty;
    wire [16:0] fifo_q = fifo[rp[8:0]];
    assign m_axis_tdata = fifo_q[15:0];
    assign m_axis_tlast = fifo_q[16];
    always @(posedge clk) begin
        if (rst || start_p) begin
            wp <= 0;
            rp <= 0;
            orow <= 0;
        end else begin
            if (c_valid) begin
                if (!fifo_full) begin
                    fifo[wp[8:0]] <= {frame_last, c_data};
                    wp <= wp + 1'b1;
                end
                if (c_last) orow <= orow + 1'b1;
            end
            if (m_axis_tvalid && m_axis_tready) rp <= rp + 1'b1;
        end
    end

    // ------------------------------------------------------------------
    // Frame control
    // ------------------------------------------------------------------
    assign irq = irq_en & done_st;
    always @(posedge clk) begin
        if (!s_axi_aresetn || soft_rst) begin
            running <= 1'b0;
            cyc_run <= 0;
            if (!s_axi_aresetn) frames <= 0;
        end else begin
            if (start_p) begin
                running <= 1'b1;
                cyc_run <= 0;
            end else if (running) begin
                cyc_run <= cyc_run + 1;
                if (c_done) begin
                    running <= 1'b0;
                    cycles <= cyc_run + 1;
                    frames <= frames + 1;
                end
            end
        end
    end

    // ------------------------------------------------------------------
    // AXI4-Lite
    // ------------------------------------------------------------------
    reg        aw_hold = 1'b0, w_hold = 1'b0;
    reg [11:0] aw_addr_q = 0;
    reg [31:0] w_data_q = 0;
    reg [3:0]  w_strb_q = 0;
    assign s_axi_awready = ~aw_hold & ~s_axi_bvalid;
    assign s_axi_wready  = ~w_hold & ~s_axi_bvalid;
    assign s_axi_bresp   = 2'b00;
    assign s_axi_rresp   = 2'b00;
    assign s_axi_arready = ~s_axi_rvalid;
    wire do_write = aw_hold & w_hold & ~s_axi_bvalid;

    always @(posedge clk) begin
        start_p <= 1'b0;
        soft_rst <= 1'b0;
        if (!s_axi_aresetn) begin
            aw_hold <= 1'b0;
            w_hold <= 1'b0;
            s_axi_bvalid <= 1'b0;
            irq_en <= 1'b0;
            done_st <= 1'b0;
            ovf_st <= 1'b0;
            bad_st <= 1'b0;
        end else begin
            if (s_axi_awvalid && s_axi_awready) begin
                aw_hold <= 1'b1;
                aw_addr_q <= s_axi_awaddr;
            end
            if (s_axi_wvalid && s_axi_wready) begin
                w_hold <= 1'b1;
                w_data_q <= s_axi_wdata;
                w_strb_q <= s_axi_wstrb;
            end
            if (s_axi_bvalid && s_axi_bready) s_axi_bvalid <= 1'b0;
            if (c_done) done_st <= 1'b1;
            if (c_valid && fifo_full) ovf_st <= 1'b1;
            if (do_write) begin
                aw_hold <= 1'b0;
                w_hold <= 1'b0;
                s_axi_bvalid <= 1'b1;
                if (w_strb_q[0]) case ({aw_addr_q[11:2], 2'b00})
                    REG_WIDTH:  if (!running) width <= w_data_q[XW-1:0];
                    REG_HEIGHT: if (!running) height <= w_data_q[XW-1:0];
                    REG_IRQ_EN: irq_en <= w_data_q[0];
                    REG_STATUS: begin
                        if (w_data_q[1]) done_st <= 1'b0;
                        if (w_data_q[2]) ovf_st <= 1'b0;
                        if (w_data_q[3]) bad_st <= 1'b0;
                    end
                    REG_CTRL: begin
                        if (w_data_q[1]) soft_rst <= 1'b1;
                        else if (w_data_q[0] && !running && !c_busy) begin
                            if (width >= 16 && width <= MAXW && height >= 16) begin
                                start_p <= 1'b1;
                                done_st <= 1'b0;
                                ovf_st <= 1'b0;
                            end else bad_st <= 1'b1;
                        end
                    end
                    default: ;
                endcase
            end
        end
    end

    always @(posedge clk) begin
        if (!s_axi_aresetn) begin
            s_axi_rvalid <= 1'b0;
        end else begin
            if (s_axi_arvalid && s_axi_arready) begin
                s_axi_rvalid <= 1'b1;
                case ({s_axi_araddr[11:2], 2'b00})
                    REG_ID:      s_axi_rdata <= ID_VALUE;
                    REG_VERSION: s_axi_rdata <= VERSION_VALUE;
                    REG_CONFIG:  s_axi_rdata <= {LRTOL[3:0], UNIQ[3:0], P2[7:0], P1[7:0], D[7:0]};
                    REG_MAXW:    s_axi_rdata <= MAXW;
                    REG_WIDTH:   s_axi_rdata <= width;
                    REG_HEIGHT:  s_axi_rdata <= height;
                    REG_STATUS:  s_axi_rdata <= {28'd0, bad_st, ovf_st, done_st, running | c_busy};
                    REG_IRQ_EN:  s_axi_rdata <= {31'd0, irq_en};
                    REG_CYCLES:  s_axi_rdata <= cycles;
                    REG_FRAMES:  s_axi_rdata <= frames;
                    default:     s_axi_rdata <= 32'd0;
                endcase
            end else if (s_axi_rvalid && s_axi_rready) s_axi_rvalid <= 1'b0;
        end
    end
endmodule
