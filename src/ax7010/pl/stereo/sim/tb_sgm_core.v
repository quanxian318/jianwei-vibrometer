// Self-checking testbench: sgm_core against golden vectors from the C++
// model (linux/vibmon/tools/sgm_vectors). +VEC=<file>
`timescale 1ns / 1ps
module tb_sgm_core;
    reg clk = 1'b0;
    always #5 clk = ~clk;
    reg rst = 1'b1, start = 1'b0, in_valid = 1'b0;
    reg [9:0] w = 0, h = 0;
    reg [30:0] cl = 0, cr = 0;
    reg [7:0] gray = 0;
    wire in_ready, out_valid, frame_done, busy;
    wire [15:0] out_q4;
    wire [9:0] out_x, out_y;

    sgm_core #(.MAXW(128)) dut (
        .clk(clk), .rst(rst), .start(start), .w(w), .h(h),
        .in_valid(in_valid), .in_ready(in_ready), .in_cl(cl), .in_cr(cr), .in_gray(gray),
        .out_valid(out_valid), .out_q4(out_q4), .out_x(out_x), .out_y(out_y),
        .frame_done(frame_done), .busy(busy));

    reg [30:0] mcl [0:16383];
    reg [30:0] mcr [0:16383];
    reg [7:0]  mg  [0:16383];
    reg [15:0] exp_q [0:16383];
    integer fd, rc, n, i, iw, ih, outs, errs, valid, cycles;
    reg [31:0] a, b, c, a2;
    reg [8*256-1:0] path;

    initial begin
        if (!$value$plusargs("VEC=%s", path)) path = "sgm_vectors.txt";
        fd = $fopen(path, "r");
        if (fd == 0) begin $display("FAIL cannot open %0s", path); $finish_and_return(1); end
        rc = $fscanf(fd, "%d %d", iw, ih);
        n = iw * ih;
        for (i = 0; i < n; i = i + 1) begin
            rc = $fscanf(fd, "%h %h %h %h", a, b, c, a2);
            mcl[i] = a[30:0]; mcr[i] = b[30:0]; mg[i] = c[7:0];
        end
        for (i = 0; i < n; i = i + 1) begin
            rc = $fscanf(fd, "%h", a);
            exp_q[i] = a[15:0];
        end
        $fclose(fd);
        repeat (3) @(posedge clk);
        rst <= 1'b0;
        w <= iw; h <= ih;
        @(posedge clk) start <= 1'b1;
        @(posedge clk) start <= 1'b0;
        i = 0;
        while (i < n) begin
            in_valid <= 1'b1; cl <= mcl[i]; cr <= mcr[i]; gray <= mg[i];
            @(posedge clk);
            if (in_ready) i = i + 1;
        end
        in_valid <= 1'b0;
    end

    // Output checker and cycle counter.
    initial begin
        outs = 0; errs = 0; valid = 0; cycles = 0;
        @(negedge rst);
        while (!frame_done && cycles < 20000000) begin
            @(posedge clk);
            cycles = cycles + 1;
            if (out_valid) begin
                if (out_x + out_y * iw != outs) begin
                    $display("FAIL order: got (%0d,%0d) at output %0d", out_x, out_y, outs);
                    errs = errs + 1;
                end
                if (out_q4 !== exp_q[outs]) begin
                    if (errs < 10) $display("FAIL pixel (%0d,%0d): got %h expected %h", out_x, out_y, out_q4, exp_q[outs]);
                    errs = errs + 1;
                end
                if (!out_q4[15]) valid = valid + 1;
                outs = outs + 1;
            end
        end
        if (!frame_done || outs != n || errs != 0) begin
            $display("sgm_core: FAILED (%0d outputs of %0d, %0d mismatches, done %0d)", outs, n, errs, frame_done);
            $finish_and_return(1);
        end
        $display("sgm_core: %0dx%0d bit-exact, %0d valid, %0d cycles (%0.1f per pixel)", iw, ih, valid, cycles, cycles * 1.0 / n);
        $finish;
    end
endmodule
