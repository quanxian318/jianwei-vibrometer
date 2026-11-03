// Self-checking testbench: lk_accum against golden vectors from the C++ model
// (linux/vibmon/tools/lk_vectors). Loads every case through the RAM write
// ports, runs one pass and compares the five sums bit by bit.
//   +VEC=<file>  (default lk_vectors.txt)
`timescale 1ns / 1ps
module tb_lk_accum;
    reg clk = 1'b0;
    always #5 clk = ~clk;
    reg rst = 1'b1;
    reg win_we = 1'b0, coef_we = 1'b0, start = 1'b0;
    reg [15:0] win_addr = 0, coef_addr = 0;
    reg [7:0] win_data = 0;
    reg [47:0] coef_data = 0;
    reg [7:0] w = 0, h = 0, ox = 0, oy = 0, ax = 0, ay = 0;
    reg [15:0] stride = 0;
    wire busy, done;
    wire [31:0] cycles;
    wire signed [47:0] s1, s2, sx, sy, st;

    lk_accum dut (
        .clk(clk), .rst(rst),
        .win_we(win_we), .win_addr(win_addr), .win_data(win_data),
        .coef_we(coef_we), .coef_addr(coef_addr), .coef_data(coef_data),
        .start(start), .w(w), .h(h), .stride(stride), .ox(ox), .oy(oy), .ax(ax), .ay(ay),
        .busy(busy), .done(done), .cycles(cycles),
        .s1(s1), .s2(s2), .sx(sx), .sy(sy), .st(st));

    integer fd, ncases, k, i, n, rows, failures, rc, iw, ih, istr, iox, ioy, iax, iay, timeout;
    reg [7:0] byte;
    reg [15:0] t16, gx16, gy16;
    reg [47:0] e1, e2, ex, ey, et;
    reg [8*256-1:0] path;

    initial begin
        failures = 0;
        if (!$value$plusargs("VEC=%s", path)) path = "lk_vectors.txt";
        fd = $fopen(path, "r");
        if (fd == 0) begin
            $display("FAIL cannot open %0s", path);
            $finish_and_return(1);
        end
        rc = $fscanf(fd, "%d", ncases);
        repeat (4) @(posedge clk);
        rst <= 1'b0;
        for (k = 0; k < ncases; k = k + 1) begin
            rc = $fscanf(fd, "%d %d %d %d %d %d %d %d", iw, ih, istr, rows, iox, ioy, iax, iay);
            for (i = 0; i < istr * rows; i = i + 1) begin
                rc = $fscanf(fd, "%h", byte);
                @(posedge clk);
                win_we <= 1'b1; win_addr <= i; win_data <= byte;
            end
            n = iw * ih;
            for (i = 0; i < n; i = i + 1) begin
                rc = $fscanf(fd, "%h %h %h", t16, gx16, gy16);
                @(posedge clk);
                win_we <= 1'b0;
                coef_we <= 1'b1; coef_addr <= i; coef_data <= {t16, gx16, gy16};
            end
            rc = $fscanf(fd, "%h %h %h %h %h", e1, e2, ex, ey, et);
            @(posedge clk);
            coef_we <= 1'b0;
            w <= iw; h <= ih; stride <= istr; ox <= iox; oy <= ioy; ax <= iax; ay <= iay;
            start <= 1'b1;
            @(posedge clk);
            start <= 1'b0;
            timeout = 0;
            while (!done && timeout < 100000) begin
                @(posedge clk);
                timeout = timeout + 1;
            end
            if (!done) begin
                $display("FAIL case %0d: no done", k);
                failures = failures + 1;
            end else if (s1 !== e1 || s2 !== e2 || sx !== ex || sy !== ey || st !== et) begin
                $display("FAIL case %0d (ox %0d oy %0d ax %0d ay %0d): got %h %h %h %h %h expected %h %h %h %h %h",
                         k, iox, ioy, iax, iay, s1, s2, sx, sy, st, e1, e2, ex, ey, et);
                failures = failures + 1;
            end else begin
                $display("case %0d ok: %0dx%0d ox %0d oy %0d ax %0d ay %0d, %0d cycles", k, iw, ih, iox, ioy, iax, iay, cycles);
            end
        end
        $fclose(fd);
        if (failures) begin
            $display("lk_accum: FAILED %0d of %0d cases", failures, ncases);
            $finish_and_return(1);
        end
        $display("lk_accum: all %0d cases bit-exact", ncases);
        $finish;
    end
endmodule
