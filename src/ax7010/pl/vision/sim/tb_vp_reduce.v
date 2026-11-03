// Streams frames from reduce_in.hex through vp_reduce with random input
// gaps and writes the output pixels to reduce_out.hex (driven by
// pc_sim/run_rtl.py, which compares them with the Python model).
`timescale 1ns / 1ps
module tb_vp_reduce;
    parameter W = 640;
    parameter H = 480;
    parameter N = 2;
    parameter MAXW = 640;
    parameter SEED = 1;
    localparam [15:0] H16 = H;

    reg clk = 1'b0;
    always #5 clk = ~clk;
    reg rst = 1'b1;
    reg in_valid = 1'b0, in_sof = 1'b0, in_eol = 1'b0;
    reg [7:0] in_data = 8'd0;
    wire out_valid, out_sof, out_eol, out_eof, overrun;
    wire [7:0] out_data;

    vp_reduce #(.MAXW(MAXW)) dut (
        .clk(clk), .rst(rst), .height(H16), .in_valid(in_valid), .in_data(in_data), .in_sof(in_sof),
        .in_eol(in_eol), .out_valid(out_valid), .out_data(out_data), .out_sof(out_sof), .out_eol(out_eol),
        .out_eof(out_eof), .overrun(overrun));

    reg [7:0] mem [0:W*H*N-1];
    integer fd, seed, f, x, y, outs, rows, eofs, sofs, errors;
    initial begin
        $readmemh("reduce_in.hex", mem);
        fd = $fopen("reduce_out.hex", "w");
        seed = SEED;
        outs = 0;
        rows = 0;
        eofs = 0;
        sofs = 0;
        errors = 0;
        repeat (5) @(posedge clk);
        rst <= 1'b0;
        for (f = 0; f < N; f = f + 1) begin
            for (y = 0; y < H; y = y + 1) begin
                for (x = 0; x < W; x = x + 1) begin
                    while (($random(seed) & 3) == 0) @(posedge clk);  // ~25 % idle cycles
                    in_valid <= 1'b1;
                    in_data <= mem[(f * H + y) * W + x];
                    in_sof <= (x == 0 && y == 0);
                    in_eol <= (x == W - 1);
                    @(posedge clk);
                    in_valid <= 1'b0;
                    in_sof <= 1'b0;
                    in_eol <= 1'b0;
                end
                repeat ($random(seed) & 7) @(posedge clk);  // horizontal blanking
            end
            repeat (W) @(posedge clk);  // vertical blanking (odd heights flush here)
        end
        repeat (20) @(posedge clk);
        if (overrun) errors = errors + 1;
        if (eofs != N || sofs != N) errors = errors + 1;
        $display("tb_vp_reduce: %0d outputs, %0d rows, %0d frames, overrun %0d, errors %0d", outs, rows, eofs,
                 overrun, errors);
        $fclose(fd);
        $finish;
    end

    always @(posedge clk) begin
        if (out_valid) begin
            $fwrite(fd, "%02x\n", out_data);
            outs = outs + 1;
            if (out_eol) rows = rows + 1;
            if (out_eof) eofs = eofs + 1;
            if (out_sof) sofs = sofs + 1;
        end
    end
endmodule
