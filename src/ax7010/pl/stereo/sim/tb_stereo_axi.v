// Self-checking testbench of the block-design wrapper: AXI4-Lite register
// programming as done by linux/vibmon/src/drv/stereo_pl.cpp, input and
// output AXI4-Stream with random stalls on both sides (DMA behaviour),
// tlast on the last pixel, DONE interrupt, CYCLES/FRAMES, two frames.
`timescale 1ns / 1ps
module tb_stereo_axi;
    reg clk = 1'b0;
    always #5 clk = ~clk;
    reg aresetn = 1'b0;
    reg [11:0] awaddr = 0, araddr = 0;
    reg awvalid = 0, wvalid = 0, bready = 1, arvalid = 0, rready = 1;
    reg [31:0] wdata = 0;
    wire awready, wready, bvalid, arready, rvalid, irq;
    wire [1:0] bresp, rresp;
    wire [31:0] rdata;
    reg [15:0] s_tdata = 0;
    reg s_tvalid = 0, m_tready = 0;
    wire s_tready, m_tvalid, m_tlast;
    wire [15:0] m_tdata;

    stereo_axi #(.MAXW(128)) dut (
        .s_axi_aclk(clk), .s_axi_aresetn(aresetn),
        .s_axi_awaddr(awaddr), .s_axi_awprot(3'd0), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(4'hf), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arprot(3'd0), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready),
        .s_axis_tdata(s_tdata), .s_axis_tvalid(s_tvalid), .s_axis_tready(s_tready), .s_axis_tlast(1'b0),
        .m_axis_tdata(m_tdata), .m_axis_tvalid(m_tvalid), .m_axis_tready(m_tready), .m_axis_tlast(m_tlast),
        .irq(irq));

    task axi_write(input [11:0] a, input [31:0] d);
        begin
            @(posedge clk);
            awaddr <= a; awvalid <= 1; wdata <= d; wvalid <= 1;
            while (!(awvalid && awready) || !(wvalid && wready)) begin
                @(posedge clk);
                if (awvalid && awready) awvalid <= 0;
                if (wvalid && wready) wvalid <= 0;
                if (!awvalid && !wvalid) disable axi_write;
            end
            @(posedge clk); awvalid <= 0; wvalid <= 0;
            while (!bvalid) @(posedge clk);
        end
    endtask
    task axi_read(input [11:0] a, output [31:0] d);
        begin
            @(posedge clk);
            araddr <= a; arvalid <= 1;
            @(posedge clk);
            while (!arready) @(posedge clk);
            arvalid <= 0;
            while (!rvalid) @(posedge clk);
            d = rdata;
        end
    endtask

    reg [7:0]  gl [0:16383];
    reg [7:0]  gr [0:16383];
    reg [15:0] exp_q [0:16383];
    integer fd, rc, n, i, o, iw, ih, errs, frame, seed, lastpos;
    reg [31:0] a, b, c, d, v;
    reg [8*256-1:0] path;
    reg feeding;

    // Input stream with random gaps.
    always @(posedge clk) begin
        if (feeding) begin
            if (s_tvalid && s_tready) i = i + 1;
            if (i < n && ($random(seed) & 3) != 0) begin
                s_tvalid <= 1; s_tdata <= {gl[i], gr[i]};
            end else s_tvalid <= 0;
            if (s_tvalid && s_tready && i >= n) s_tvalid <= 0;
        end else s_tvalid <= 0;
    end
    // Output stream with random back-pressure; check every beat.
    always @(posedge clk) begin
        m_tready <= ($random(seed) % 8) != 0;
        if (m_tvalid && m_tready) begin
            if (m_tdata !== exp_q[o]) begin
                if (errs < 8) $display("FAIL frame %0d pixel %0d: got %h expected %h", frame, o, m_tdata, exp_q[o]);
                errs = errs + 1;
            end
            if (m_tlast) lastpos = o;
            o = o + 1;
        end
    end

    initial begin
        seed = 11; errs = 0; feeding = 0; lastpos = -1;
        if (!$value$plusargs("VEC=%s", path)) path = "vec.txt";
        fd = $fopen(path, "r");
        if (fd == 0) begin $display("FAIL cannot open %0s", path); $finish_and_return(1); end
        rc = $fscanf(fd, "%d %d", iw, ih);
        n = iw * ih;
        for (i = 0; i < n; i = i + 1) begin
            rc = $fscanf(fd, "%h %h %h %h", a, b, c, d);
            gl[i] = c[7:0]; gr[i] = d[7:0];
        end
        for (i = 0; i < n; i = i + 1) begin rc = $fscanf(fd, "%h", a); exp_q[i] = a[15:0]; end
        $fclose(fd);
        repeat (5) @(posedge clk);
        aresetn <= 1;
        axi_read(12'h000, v); if (v !== 32'h53544552) begin $display("FAIL ID %h", v); errs = errs + 1; end
        axi_read(12'h008, v); if (v !== {4'd1, 4'd5, 8'd86, 8'd7, 8'd64}) begin $display("FAIL CONFIG %h", v); errs = errs + 1; end
        axi_read(12'h00C, v); if (v !== 128) begin $display("FAIL MAXW %0d", v); errs = errs + 1; end
        // A size beyond MAXW is refused with BAD_SIZE.
        axi_write(12'h010, 200); axi_write(12'h014, ih); axi_write(12'h018, 1);
        axi_read(12'h01C, v); if (v[3] !== 1'b1 || v[0] !== 1'b0) begin $display("FAIL BAD_SIZE status %h", v); errs = errs + 1; end
        axi_write(12'h01C, 32'h8);
        for (frame = 0; frame < 2; frame = frame + 1) begin
            axi_write(12'h010, iw); axi_write(12'h014, ih); axi_write(12'h020, 1);
            o = 0; i = 0; lastpos = -1;
            axi_write(12'h018, 1);   // START, then the "DMA" streams run
            feeding = 1;
            while (!irq) @(posedge clk);
            while (o < n) @(posedge clk);
            feeding = 0;
            axi_read(12'h01C, v);
            if (v[1] !== 1'b1 || v[2] !== 1'b0) begin $display("FAIL status after frame %h", v); errs = errs + 1; end
            axi_read(12'h024, v);
            $display("frame %0d: %0d cycles (%0.1f per pixel)", frame, v, v * 1.0 / n);
            axi_write(12'h01C, 32'h2);  // clear DONE
            if (irq) begin $display("FAIL irq still high after W1C"); errs = errs + 1; end
            if (lastpos != n - 1) begin $display("FAIL tlast at %0d, expected %0d", lastpos, n - 1); errs = errs + 1; end
            repeat (20) @(posedge clk);
            if (m_tvalid) begin $display("FAIL extra output beats"); errs = errs + 1; end
        end
        axi_read(12'h028, v); if (v !== 2) begin $display("FAIL FRAMES %0d", v); errs = errs + 1; end
        if (errs) begin $display("stereo_axi: FAILED (%0d errors)", errs); $finish_and_return(1); end
        $display("stereo_axi: %0dx%0d, 2 frames bit-exact through AXI-Lite + AXI-Stream with random stalls", iw, ih);
        $finish;
    end
endmodule
