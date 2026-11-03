// Timestamped event capture. Every source has its own pending slot that
// stores the timestamp of the edge itself, so simultaneous edges keep exact
// times; an arbiter moves one pending event per clock into a FIFO.
// Entry: info = {seq[15:0], 7'b0, level, 3'b0, source[4:0]}, ts[63:0].
`timescale 1ns / 1ps
module cc_events #(
    parameter NSRC = 21,
    parameter DEPTH_LOG2 = 9
) (
    input  wire              clk,
    input  wire              rst,
    input  wire [63:0]       ts,
    input  wire [NSRC-1:0]   hit,      // one-clock strobe: enabled edge seen
    input  wire [NSRC-1:0]   lvl,      // level recorded with the event
    input  wire              pop,
    input  wire              clear,
    output wire [DEPTH_LOG2:0] count,
    output reg               overflow = 1'b0,
    output reg  [31:0]       dropped = 32'd0,
    output wire [31:0]       head_info,
    output wire [63:0]       head_ts
);
    localparam DEPTH = 1 << DEPTH_LOG2;

    reg [NSRC-1:0] pend = {NSRC{1'b0}};
    reg [NSRC-1:0] plvl = {NSRC{1'b0}};
    reg [63:0]     pts [0:NSRC-1];

    reg [95:0] mem [0:DEPTH-1];
    reg [95:0] head_q = 96'd0;
    reg [DEPTH_LOG2-1:0] wptr = 0, rptr = 0;
    reg [DEPTH_LOG2:0]   cnt = 0;
    reg [15:0]           seq = 16'd0;

    assign count = cnt;
    assign head_info = head_q[95:64];
    assign head_ts = head_q[63:0];

    // Lowest pending source index wins.
    integer k;
    reg [4:0] sel;
    reg       sel_valid;
    always @* begin
        sel = 5'd0;
        sel_valid = 1'b0;
        for (k = NSRC - 1; k >= 0; k = k - 1) begin
            if (pend[k]) begin
                sel = k;
                sel_valid = 1'b1;
            end
        end
    end

    wire full  = (cnt == DEPTH);
    wire empty = (cnt == 0);
    wire wr    = sel_valid & ~full;
    wire drop  = sel_valid & full;
    wire rd    = pop & ~empty;
    wire [NSRC-1:0] grant = (sel_valid ? ({{(NSRC-1){1'b0}}, 1'b1} << sel) : {NSRC{1'b0}});
    wire lost_pending = |(hit & pend & ~grant);

    integer i;
    always @(posedge clk) begin
        if (rst || clear) begin
            pend     <= {NSRC{1'b0}};
            wptr     <= 0;
            rptr     <= 0;
            cnt      <= 0;
            overflow <= 1'b0;
            dropped  <= 32'd0;
            if (rst) seq <= 16'd0;
        end else begin
            for (i = 0; i < NSRC; i = i + 1) begin
                if (hit[i] && !(pend[i] && !grant[i])) begin
                    pend[i] <= 1'b1;
                    pts[i]  <= ts;
                    plvl[i] <= lvl[i];
                end else if (grant[i]) begin
                    pend[i] <= 1'b0;
                end
            end
            if (wr) begin
                mem[wptr] <= {seq, 7'd0, plvl[sel], 3'd0, sel, pts[sel]};
                wptr <= wptr + 1'b1;
                seq  <= seq + 16'd1;
            end
            if (rd) rptr <= rptr + 1'b1;
            cnt <= cnt + (wr ? 1'b1 : 1'b0) - (rd ? 1'b1 : 1'b0);
            if (drop || lost_pending) begin
                overflow <= 1'b1;
                if (dropped != 32'hFFFF_FFFF) dropped <= dropped + 32'd1;
            end
        end
        head_q <= mem[rptr];
    end
endmodule
