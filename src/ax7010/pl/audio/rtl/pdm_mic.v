// MP34DT01 PDM microphone receiver on J11-32/33 (J706 through J801), one
// AXI4-Lite slave (4 KiB). The PL keeps the timing-critical part; the PS
// decimates the bit stream (linux/vibmon measure/pdm_model, CIC 5/32 + FIR /2
// to 31.25 kS/s) - the board design has no slices left for the filter.
//
//  * mic_clk: 2 MHz, 50 % duty from a register (held low while disabled,
//    killed or in reset; HALF_DIV system clocks per half period);
//  * mic_dat: oversampled by the system clock through a two-stage
//    synchronizer, one bit per PDM period at SAMPLE_AT clocks after the edge
//    that opens the valid half (L/R = GND: data while mic_clk is low, PHASE =
//    0; L/R = VDD: while high, PHASE = 1). The other half reads the board
//    pull-up, so the wrong phase or a missing module gives all ones;
//  * 32 bits per word (bit 0 = oldest) into a FIFO of 2^FIFO_LOG2 words read
//    one word per AXI read. A word arriving at a full FIFO sets OVERFLOW,
//    and from then on every word is dropped (and counted) until the PS clears
//    OVERFLOW: the FIFO never holds words from both sides of a gap;
//  * WORDS (words completed since enable, dropped ones included) latches the
//    FIFO level, DROPPED, the bits of the word being filled and the clock
//    counter at the same instant: the word at the head has the index
//    WORDS - DROPPED - LEVEL, and the newest bit was taken at about TS.
//
// Register map (mirrored in linux/vibmon/src/drv/pdm_regs.h; a unit test
// compares the two files):
//   0x000 ID        ro  0x50444D31 ("PDM1")
//   0x004 VERSION   ro  0x00020000
//   0x008 CLK_HZ    ro  CLK_HZ
//   0x00C CTRL      rw  bit0 EN (0 -> 1 restarts the counters), bit1 PHASE
//                       (1 = data while mic_clk is high), bit8 CLEAR (empties the FIFO)
//   0x010 HALF_DIV  rw  system clocks per half period (>= 4)
//   0x014 SAMPLE_AT rw  clocks after the opening edge at which the bit is taken (< HALF_DIV)
//   0x018 STATUS    r/w1c bit0 RUNNING, bit1 OVERFLOW (w1c, also zeroes DROPPED), bit2 KILLED
//   0x01C LEVEL     ro  words in the FIFO
//   0x020 DATA      ro  pops the head word (0 when empty)
//   0x024 WORDS     ro  words completed since enable; latches the four _L registers
//   0x028 LEVEL_L   ro
//   0x02C BITS_L    ro  bits of the word being filled
//   0x030 TS_L      ro  clock counter
//   0x034 TS        ro  free-running clock counter
//   0x038 DROPPED   ro  words dropped since OVERFLOW was last cleared
//   0x03C DROPPED_L ro
//   0x040 CONFIG    ro  [15:8] FIFO_LOG2, [7:0] bits per word (32)
`timescale 1ns / 1ps
module pdm_mic #(
    parameter CLK_HZ    = 100000000,
    parameter FIFO_LOG2 = 13
) (
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
    output reg         s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [11:0] s_axi_araddr,
    input  wire [2:0]  s_axi_arprot,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output reg  [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output reg         s_axi_rvalid,
    input  wire        s_axi_rready,

    (* X_INTERFACE_IGNORE = "true" *)  // a plain output, not a clock interface of the block design
    output wire mic_clk,
    input  wire mic_dat,
    input  wire kill      // 1 stops the clock (power fail); tie 0 when unused
);
    localparam [31:0] ID_VALUE = 32'h5044_4D31;  // "PDM1"
    localparam [31:0] VERSION  = 32'h0002_0000;  // 2.x: raw PDM words
    localparam [7:0]  HALF_DEFAULT = CLK_HZ / 4000000;  // 2 MHz
    localparam [7:0]  FIFO_LOG2_B = FIFO_LOG2;
    localparam [31:0] CONFIG = {16'd0, FIFO_LOG2_B, 8'd32};  // FIFO depth, bits per word
    localparam integer DEPTH = 1 << FIFO_LOG2;

    wire clk = s_axi_aclk;
    wire rst = ~s_axi_aresetn;

    reg        en = 1'b0, phase = 1'b0, clear = 1'b0;
    reg [7:0]  half_div = HALF_DEFAULT;
    reg [7:0]  sample_at = (HALF_DEFAULT * 3) / 4;
    reg [31:0] ts = 32'd0;
    always @(posedge clk) ts <= ts + 32'd1;

    // ------------------------------------------------------------------
    // Enable, kill, PDM clock, sampling
    // ------------------------------------------------------------------
    reg [1:0] kill_s = 2'b00;
    reg       killed = 1'b0, en_q = 1'b0;
    wire      start = en & ~en_q;
    wire      running = en & ~killed;
    always @(posedge clk) begin
        kill_s <= {kill_s[0], kill};
        en_q <= rst ? 1'b0 : en;
        if (rst || start) killed <= 1'b0;
        else if (kill_s[1] && en) killed <= 1'b1;
    end

    reg [7:0] cnt = 8'd0;
    reg       clk_q = 1'b0;
    reg       clk_pad = 1'b0;  // drives the pin
    always @(posedge clk) begin
        if (rst || !running) begin
            cnt <= 8'd0;
            clk_q <= 1'b0;
            clk_pad <= 1'b0;
        end else if (cnt >= half_div - 8'd1) begin
            cnt <= 8'd0;
            clk_q <= ~clk_q;
            clk_pad <= ~clk_q;
        end else begin
            cnt <= cnt + 8'd1;
        end
    end
    assign mic_clk = clk_pad;

    reg dat_meta = 1'b1;
    reg dat_s = 1'b1;
    always @(posedge clk) begin
        dat_meta <= mic_dat;
        dat_s <= dat_meta;
    end
    wire take = running && cnt == sample_at && clk_q == phase;

    // ------------------------------------------------------------------
    // 32-bit words, FIFO, counters
    // ------------------------------------------------------------------
    reg [31:0] word = 32'd0;
    reg [4:0]  bits = 5'd0;
    reg        word_done = 1'b0;
    always @(posedge clk) begin
        word_done <= 1'b0;
        if (rst || start) begin
            bits <= 5'd0;
        end else if (take) begin
            word <= {dat_s, word[31:1]};
            bits <= bits + 5'd1;
            word_done <= &bits;
        end
    end

    reg [31:0] fifo [0:DEPTH-1];
    reg [FIFO_LOG2-1:0] wr_ptr = 0, rd_ptr = 0;
    reg [FIFO_LOG2:0]   level = 0, level_q = 0;  // level_q: what the read side may use (head is valid)
    reg [31:0]          head = 32'd0;
    reg                 pop = 1'b0;
    reg [31:0]          words = 32'd0;
    reg [31:0]          dropped = 32'd0;
    reg                 overflow = 1'b0, ovf_w1c = 1'b0;
    wire                full = level[FIFO_LOG2];
    wire                push = word_done && !full && !overflow;
    wire                pop_ok = pop && level != 0;
    always @(posedge clk) begin
        head <= fifo[rd_ptr];
        level_q <= level;
        if (push) fifo[wr_ptr] <= word;
    end
    always @(posedge clk) begin
        if (rst || start || clear) begin
            rd_ptr <= wr_ptr;
            level <= 0;
            dropped <= 32'd0;
            overflow <= 1'b0;
        end else begin
            if (push) wr_ptr <= wr_ptr + 1'b1;
            if (pop_ok) rd_ptr <= rd_ptr + 1'b1;
            level <= level + (push ? 1'b1 : 1'b0) - (pop_ok ? 1'b1 : 1'b0);
            if (word_done && (full || overflow)) begin
                dropped <= dropped + 32'd1;
                overflow <= 1'b1;
            end else if (ovf_w1c) begin  // the PS has taken the words before the gap
                overflow <= 1'b0;
                dropped <= 32'd0;
            end
        end
        if (rst || start) words <= 32'd0;
        else if (word_done) words <= words + 32'd1;
    end

    // ------------------------------------------------------------------
    // AXI4-Lite: a write is taken when address and data are both there
    // ------------------------------------------------------------------
    wire wr = s_axi_awvalid && s_axi_wvalid && !s_axi_bvalid;
    assign s_axi_awready = wr;
    assign s_axi_wready  = wr;
    assign s_axi_bresp   = 2'b00;
    assign s_axi_rresp   = 2'b00;
    wire [31:0] wd = s_axi_wdata;
    always @(posedge clk) begin
        clear <= 1'b0;
        ovf_w1c <= 1'b0;
        if (rst) begin
            s_axi_bvalid <= 1'b0;
            en <= 1'b0;
        end else begin
            if (s_axi_bvalid && s_axi_bready) s_axi_bvalid <= 1'b0;
            if (wr) begin
                s_axi_bvalid <= 1'b1;
                case (s_axi_awaddr[11:2])
                    10'h003: begin  // CTRL
                        if (s_axi_wstrb[0]) begin
                            en <= wd[0];
                            phase <= wd[1];
                        end
                        if (s_axi_wstrb[1]) clear <= wd[8];
                    end
                    10'h004: if (s_axi_wstrb[0]) half_div <= wd[7:0] < 8'd4 ? 8'd4 : wd[7:0];
                    10'h005: if (s_axi_wstrb[0]) sample_at <= wd[7:0];
                    10'h006: if (s_axi_wstrb[0]) ovf_w1c <= wd[1];  // STATUS: 1 clears OVERFLOW and DROPPED
                    default: ;
                endcase
            end
            if (sample_at >= half_div) sample_at <= half_div - 8'd1;
        end
    end

    // Reads: address latched, data one clock later; DATA pops the head and
    // WORDS latches the level, the partial word and the clock counter.
    reg        rd_busy = 1'b0;
    reg [9:0]  ar_q = 10'd0;
    reg [FIFO_LOG2:0] level_l = 0;
    reg [4:0]  bits_l = 5'd0;
    reg [31:0] ts_l = 32'd0, dropped_l = 32'd0;
    assign s_axi_arready = ~rd_busy & ~s_axi_rvalid;
    always @(posedge clk) begin
        pop <= 1'b0;
        if (rst) begin
            rd_busy <= 1'b0;
            s_axi_rvalid <= 1'b0;
        end else begin
            if (s_axi_arvalid && s_axi_arready) begin
                rd_busy <= 1'b1;
                ar_q <= s_axi_araddr[11:2];
            end else if (rd_busy && !(ar_q == 10'h009 && word_done)) begin
                // (WORDS waits out the clock in which a word completes: the
                // bit count has wrapped, WORDS and the level not yet moved.)
                rd_busy <= 1'b0;
                s_axi_rvalid <= 1'b1;
                case (ar_q)
                    10'h000: s_axi_rdata <= ID_VALUE;
                    10'h001: s_axi_rdata <= VERSION;
                    10'h002: s_axi_rdata <= CLK_HZ;
                    10'h003: s_axi_rdata <= {30'd0, phase, en};
                    10'h004: s_axi_rdata <= {24'd0, half_div};
                    10'h005: s_axi_rdata <= {24'd0, sample_at};
                    10'h006: s_axi_rdata <= {29'd0, killed, overflow, running};
                    10'h007: s_axi_rdata <= level_q;
                    10'h008: begin
                        s_axi_rdata <= level_q != 0 ? head : 32'd0;
                        pop <= level_q != 0;
                    end
                    10'h009: begin
                        s_axi_rdata <= words;
                        level_l <= level;
                        bits_l <= bits;
                        ts_l <= ts;
                        dropped_l <= dropped;
                    end
                    10'h00A: s_axi_rdata <= level_l;
                    10'h00B: s_axi_rdata <= {27'd0, bits_l};
                    10'h00C: s_axi_rdata <= ts_l;
                    10'h00D: s_axi_rdata <= ts;
                    10'h00E: s_axi_rdata <= dropped;
                    10'h00F: s_axi_rdata <= dropped_l;
                    10'h010: s_axi_rdata <= CONFIG;
                    default: s_axi_rdata <= 32'd0;
                endcase
            end
            if (s_axi_rvalid && s_axi_rready) s_axi_rvalid <= 1'b0;
        end
    end
endmodule
