// Self-checking testbench: full PL stereo path (census + SGM) fed with the
// left/right gray images of a golden vector file; compares every disparity.
`timescale 1ns / 1ps
module tb_stereo_core;
    reg clk = 1'b0;
    always #5 clk = ~clk;
    reg rst = 1'b1, start = 1'b0, s_valid = 1'b0;
    reg [9:0] w = 0, h = 0;
    reg [15:0] s_data = 0;
    wire s_ready, m_valid, m_last, frame_done, busy;
    wire [15:0] m_data;

    stereo_core #(.MAXW(128)) dut (
        .clk(clk), .rst(rst), .start(start), .w(w), .h(h),
        .s_valid(s_valid), .s_ready(s_ready), .s_data(s_data),
        .m_valid(m_valid), .m_data(m_data), .m_last(m_last), .frame_done(frame_done), .busy(busy));

    reg [7:0]  gl [0:16383];
    reg [7:0]  gr [0:16383];
    reg [15:0] exp_q [0:16383];
    integer fd, rc, n, i, iw, ih, outs, errs, valid, cycles, rows;
    reg [31:0] a, b, c, d;
    reg [8*256-1:0] path;

    initial begin
        if (!$value$plusargs("VEC=%s", path)) path = "sgm_vectors.txt";
        fd = $fopen(path, "r");
        if (fd == 0) begin $display("FAIL cannot open %0s", path); $finish_and_return(1); end
        rc = $fscanf(fd, "%d %d", iw, ih);
        n = iw * ih;
        for (i = 0; i < n; i = i + 1) begin
            rc = $fscanf(fd, "%h %h %h %h", a, b, c, d);
            gl[i] = c[7:0]; gr[i] = d[7:0];
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
            s_valid <= 1'b1; s_data <= {gl[i], gr[i]};
            @(posedge clk);
            if (s_ready) i = i + 1;
        end
        s_valid <= 1'b0;
    end

    initial begin
        outs = 0; errs = 0; valid = 0; cycles = 0; rows = 0;
        @(negedge rst);
        while (!frame_done && cycles < 20000000) begin
            @(posedge clk);
            cycles = cycles + 1;
            if (m_valid) begin
                if (m_data !== exp_q[outs]) begin
                    if (errs < 10) $display("FAIL output %0d: got %h expected %h", outs, m_data, exp_q[outs]);
                    errs = errs + 1;
                end
                if (!m_data[15]) valid = valid + 1;
                if (m_last) rows = rows + 1;
                outs = outs + 1;
            end
        end
        if (!frame_done || outs != n || errs != 0 || rows != ih) begin
            $display("stereo_core: FAILED (%0d outputs of %0d, %0d mismatches, %0d rows, done %0d)", outs, n, errs, rows, frame_done);
            $finish_and_return(1);
        end
        $display("stereo_core: %0dx%0d gray in -> disparity bit-exact, %0d valid, %0d cycles (%0.1f per pixel)",
                 iw, ih, valid, cycles, cycles * 1.0 / n);
        $finish;
    end
endmodule
