// Full-resolution frames from core_in.hex -> LEVELS x vp_reduce -> vp_core.
// After each frame the magnified G0 and the band amplitude are written to
// core_mag.hex / core_amp.hex (pc_sim/run_rtl.py compares them with the model).
`timescale 1ns / 1ps
module tb_vp_core;
    parameter W = 640;
    parameter H = 480;
    parameter N = 4;
    parameter LEVELS = 2;
    parameter COEF_H = 40000;
    parameter COEF_L = 10000;
    parameter GAIN = 20;
    parameter SEED = 7;

    function integer stage_dim(input integer d, input integer n);
        integer j;
        begin
            stage_dim = d;
            for (j = 0; j < n; j = j + 1) stage_dim = (stage_dim + 1) / 2;
        end
    endfunction
    localparam W0 = stage_dim(W, LEVELS), H0 = stage_dim(H, LEVELS);
    localparam W1 = (W0 + 1) / 2, H1 = (H0 + 1) / 2;

    reg clk = 1'b0;
    always #5 clk = ~clk;
    reg rst = 1'b1;
    reg in_valid = 1'b0, in_sof = 1'b0, in_eol = 1'b0;
    reg [7:0] in_data = 8'd0;

    // Reduce chain from the camera resolution to G0.
    wire [7:0] sd [0:LEVELS];
    wire sv [0:LEVELS];
    wire ss [0:LEVELS];
    wire se [0:LEVELS];
    assign sd[0] = in_data;
    assign sv[0] = in_valid;
    assign ss[0] = in_sof;
    assign se[0] = in_eol;
    genvar g;
    generate
        for (g = 0; g < LEVELS; g = g + 1) begin : chain
            localparam [15:0] HG = stage_dim(H, g);
            vp_reduce #(.MAXW(stage_dim(W, g))) r (.clk(clk), .rst(rst), .height(HG), .in_valid(sv[g]),
                .in_data(sd[g]), .in_sof(ss[g]), .in_eol(se[g]), .out_valid(sv[g + 1]), .out_data(sd[g + 1]),
                .out_sof(ss[g + 1]), .out_eol(se[g + 1]), .out_eof(), .overrun());
        end
    endgenerate

    wire done, busy, out_buf;
    wire [15:0] w0, h0, w1, h1;
    wire [31:0] frames, dropped;
    reg [15:0] rd_mag_addr = 0, rd_amp_addr = 0;
    wire [7:0] rd_mag;
    wire [15:0] rd_amp;
    localparam [15:0] H0_16 = H0;
    localparam [15:0] CH = COEF_H, CL = COEF_L;
    localparam [7:0] GN = GAIN;
    vp_core #(.MAXW0(W0), .MAXH0(H0)) dut (
        .clk(clk), .rst(rst), .coef_h(CH), .coef_l(CL), .gain(GN), .g0_height(H0_16),
        .in_valid(sv[LEVELS]), .in_data(sd[LEVELS]), .in_sof(ss[LEVELS]), .in_eol(se[LEVELS]), .done(done),
        .out_buf(out_buf), .w0(w0), .h0(h0), .w1(w1), .h1(h1), .busy(busy), .frames(frames), .dropped(dropped),
        .rd_mag_addr(rd_mag_addr), .rd_mag(rd_mag), .rd_amp_addr(rd_amp_addr), .rd_amp(rd_amp));

    reg [7:0] mem [0:W*H*N-1];
    integer fdm, fda, seed, f, x, y, i, errors, cycles;
    initial begin
        $readmemh("core_in.hex", mem);
        fdm = $fopen("core_mag.hex", "w");
        fda = $fopen("core_amp.hex", "w");
        seed = SEED;
        errors = 0;
        repeat (5) @(posedge clk);
        rst <= 1'b0;
        for (f = 0; f < N; f = f + 1) begin
            for (y = 0; y < H; y = y + 1) begin
                for (x = 0; x < W; x = x + 1) begin
                    while (($random(seed) & 7) == 0) @(posedge clk);
                    in_valid <= 1'b1;
                    in_data <= mem[(f * H + y) * W + x];
                    in_sof <= (x == 0 && y == 0);
                    in_eol <= (x == W - 1);
                    @(posedge clk);
                    in_valid <= 1'b0;
                    in_sof <= 1'b0;
                    in_eol <= 1'b0;
                end
                repeat ($random(seed) & 7) @(posedge clk);
            end
            // Wait for the core (vertical blanking).
            cycles = 0;
            while (!done && cycles < 2000000) begin
                @(posedge clk);
                cycles = cycles + 1;
            end
            if (!done) errors = errors + 1;
            @(posedge clk);
            if (w0 != W0 || h0 != H0 || w1 != W1 || h1 != H1) errors = errors + 1;
            for (i = 0; i < W0 * H0; i = i + 1) begin
                rd_mag_addr <= i;
                @(posedge clk);
                @(negedge clk);
                $fwrite(fdm, "%02x\n", rd_mag);
            end
            for (i = 0; i < W1 * H1; i = i + 1) begin
                rd_amp_addr <= i;
                @(posedge clk);
                @(negedge clk);
                $fwrite(fda, "%04x\n", rd_amp);
            end
        end
        if (frames != N || dropped != 0) errors = errors + 1;
        $display("tb_vp_core: %0d frames, G0 %0dx%0d, band %0dx%0d, dropped %0d, errors %0d", frames, w0, h0, w1, h1,
                 dropped, errors);
        $fclose(fdm);
        $fclose(fda);
        $finish;
    end
endmodule
