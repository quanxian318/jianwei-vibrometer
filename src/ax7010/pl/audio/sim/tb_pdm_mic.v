// pdm_mic (raw PDM words) against the bits a microphone model sends
// (vectors from linux/vibmon/tools/pdm_vectors):
//  * L/R = GND microphone (data while mic_clk is low, high impedance with the
//    board pull-up while high): every word and its index, read the way the
//    PS reads (WORDS latches LEVEL, DROPPED, BITS and TS), and the latched bit
//    count against the latched clock count;
//  * a FIFO overflow freezes the FIFO until OVERFLOW is cleared; the words
//    before and after the gap keep their indices; CLEAR;
//  * kill stops the clock until the next enable;
//  * the wrong phase reads the pull-up (all ones); L/R = VDD with PHASE = 1.
// Usage: vvp tb +VEC=vectors.txt
`timescale 1ns / 1ps
module tb_pdm_mic;
    localparam integer FIFO_LOG2 = 6, MAXW = 8192;
    localparam integer HALF = 25, AT = 18;  // defaults at 100 MHz
    localparam [11:0] R_ID = 12'h000, R_VERSION = 12'h004, R_CTRL = 12'h00C, R_STATUS = 12'h018,
                      R_LEVEL = 12'h01C, R_DATA = 12'h020, R_WORDS = 12'h024, R_LEVEL_L = 12'h028,
                      R_BITS_L = 12'h02C, R_TS_L = 12'h030, R_TS = 12'h034, R_DROPPED = 12'h038,
                      R_DROPPED_L = 12'h03C, R_CONFIG = 12'h040;

    reg clk = 1'b0, aresetn = 1'b0, kill = 1'b0;
    always #5 clk = ~clk;  // 100 MHz

    reg  [11:0] awaddr = 0, araddr = 0;
    reg         awvalid = 0, wvalid = 0, arvalid = 0;
    reg  [31:0] wdata = 0;
    reg  [3:0]  wstrb = 4'hF;
    wire        awready, wready, bvalid, arready, rvalid;
    wire [1:0]  bresp, rresp;
    wire [31:0] rdata;
    wire        mic_clk;
    tri1        mic_dat;  // R711 pull-up

    pdm_mic #(.CLK_HZ(100000000), .FIFO_LOG2(FIFO_LOG2)) dut (
        .s_axi_aclk(clk), .s_axi_aresetn(aresetn),
        .s_axi_awaddr(awaddr), .s_axi_awprot(3'd0), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(wstrb), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(1'b1),
        .s_axi_araddr(araddr), .s_axi_arprot(3'd0), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(1'b1),
        .mic_clk(mic_clk), .mic_dat(mic_dat), .kill(kill));

    // ---------------------------------------------------------------- vectors
    reg [31:0] vec [0:MAXW-1];
    integer    nwords;
    function getbit(input integer i);
        // Past the vector: alternating bits.
        getbit = i < 32 * nwords ? vec[i / 32][i % 32] : i[0];
    endfunction
    function [31:0] getword(input integer w);
        integer b;
        begin
            for (b = 0; b < 32; b = b + 1) getword[b] = getbit(32 * w + b);
        end
    endfunction

    // ------------------------------------------------------------ microphone
    // Drives DOUT 40 ns after the edge that opens its half, releases it 10 ns
    // after the closing edge (T_EN >= 18 ns, T_DIS <= 16 ns in the datasheet).
    reg     mic_on = 1'b0, mic_lr = 1'b0, dat_v = 1'b1, dat_oe = 1'b0;
    integer bit_idx = 0;
    assign mic_dat = dat_oe ? dat_v : 1'bz;
    always @(negedge mic_clk) if (mic_on) begin
        if (!mic_lr) begin
            bit_idx = bit_idx + 1;
            #40 begin dat_v = getbit(bit_idx); dat_oe = 1'b1; end
        end else begin
            #10 dat_oe = 1'b0;
        end
    end
    always @(posedge mic_clk) if (mic_on) begin
        if (mic_lr) begin
            #40 begin dat_v = getbit(bit_idx); dat_oe = 1'b1; bit_idx = bit_idx + 1; end
        end else begin
            #10 dat_oe = 1'b0;
        end
    end
    // Starts the microphone with bit 0; L/R = GND drives it in the low half already running.
    task mic_start(input lr);
        begin
            mic_lr = lr;
            bit_idx = 0;
            dat_oe = 1'b0;
            mic_on = 1'b1;
            if (!lr) begin dat_v = getbit(0); dat_oe = 1'b1; end
        end
    endtask

    // Clock count of the first cycle the core runs after an enable: the cycle
    // of the enable, or the next one when the core was killed before.
    reg [31:0] ts_start = 0;
    reg        start_seen = 1'b0;
    always @(posedge clk) begin
        if ((dut.start || start_seen) && dut.running) begin
            ts_start <= dut.ts;
            start_seen <= 1'b0;
        end else if (dut.start) begin
            start_seen <= 1'b1;
        end
    end

    // --------------------------------------------------------------- AXI-Lite
    task axi_write(input [11:0] addr, input [31:0] data);
        begin
            @(negedge clk);
            awaddr = addr; wdata = data; wstrb = 4'hF; awvalid = 1'b1; wvalid = 1'b1;
            @(posedge clk);
            while (!(awready && wready)) @(posedge clk);
            @(negedge clk);
            awvalid = 1'b0; wvalid = 1'b0;
            while (!bvalid) @(negedge clk);
            @(negedge clk);
        end
    endtask
    task axi_read(input [11:0] addr, output [31:0] data);
        begin
            @(negedge clk);
            araddr = addr; arvalid = 1'b1;
            @(posedge clk);
            while (!arready) @(posedge clk);
            @(negedge clk);
            arvalid = 1'b0;
            while (!rvalid) @(negedge clk);
            data = rdata;
            @(negedge clk);
        end
    endtask

    integer errors = 0;
    task expect(input [511:0] what, input integer got, input integer want);
        if (got !== want) begin
            errors = errors + 1;
            $display("FAIL %0s: got %0d (0x%08x), want %0d (0x%08x)", what, got, got, want, want);
        end
    endtask

    // One PS poll (drv/pdm_mic.cpp): OVERFLOW, the latched snapshot, the words
    // it says are there, and OVERFLOW cleared if the FIFO was frozen before.
    integer next_idx;   // index of the next word expected
    integer gaps;       // index jumps seen
    reg     ones_only;  // every word read was all ones
    reg     check_bits; // compare with the vector (the phase matches the microphone)
    integer phase_off;  // the first take is in the high half with PHASE = 1
    task poll;
        reg [31:0] p, l, b, t, d, w, st;
        integer i, first, takes;
        begin
            axi_read(R_STATUS, st);
            axi_read(R_WORDS, p);
            axi_read(R_LEVEL_L, l);
            axi_read(R_BITS_L, b);
            axi_read(R_TS_L, t);
            axi_read(R_DROPPED_L, d);
            // Bits taken by the latch: one per 2 x HALF clocks from SAMPLE_AT after the start.
            takes = (t - ts_start - 1 - AT - phase_off) / (2 * HALF) + 1;
            expect("bits taken by the latch", 32 * p + b, takes);
            first = p - d - l;
            if (first != next_idx) gaps = gaps + 1;
            if (first < next_idx) begin
                errors = errors + 1;
                $display("FAIL word index went back: %0d after %0d", first, next_idx);
            end
            for (i = 0; i < l; i = i + 1) begin
                axi_read(R_DATA, w);
                if (check_bits) expect("word", w, getword(first + i));
                if (w != 32'hFFFFFFFF) ones_only = 1'b0;
            end
            next_idx = first + l;
            if (st[1]) axi_write(R_STATUS, 32'h2);  // words before the gap taken: resume
        end
    endtask

    task run_until(input integer words);
        begin
            while (next_idx < words) begin
                // Varying gaps put the latch at every position within the PDM period.
                repeat (3000 + (next_idx * 7) % 50) @(posedge clk);
                poll;
            end
        end
    endtask

    reg [31:0] v, l1, l2, d1;
    reg [1023:0] path;
    integer fd, i, r;
    initial begin
        if (!$value$plusargs("VEC=%s", path)) begin
            $display("usage: +VEC=<vectors.txt>");
            $finish;
        end
        fd = $fopen(path, "r");
        if (fd == 0) begin
            $display("cannot open vectors");
            $finish;
        end
        r = $fscanf(fd, "%d\n", nwords);
        for (i = 0; i < nwords; i = i + 1) r = $fscanf(fd, "%h\n", vec[i]);
        $fclose(fd);

        repeat (10) @(posedge clk);
        aresetn = 1'b1;
        repeat (5) @(posedge clk);
        axi_read(R_ID, v);      expect("ID", v, 32'h50444D31);
        axi_read(R_VERSION, v); expect("VERSION major", v[31:16], 2);
        axi_read(R_CONFIG, v);  expect("CONFIG", v, {16'd0, 8'd6, 8'd32});
        axi_read(R_STATUS, v);  expect("idle STATUS", v, 0);
        expect("clock idle low", mic_clk, 0);

        // ---- L/R = GND: every word, read the way the PS reads.
        axi_write(R_CTRL, 32'h1);
        mic_start(1'b0);
        next_idx = 0; gaps = 0; check_bits = 1'b1; ones_only = 1'b1; phase_off = 0;
        run_until(400);
        expect("index jumps without an overflow", gaps, 0);

        // ---- Overflow: no reads until 64 words are stored and more arrive.
        repeat ((64 + 12) * 32 * 2 * HALF) @(posedge clk);
        axi_read(R_STATUS, v);  expect("overflow flag", v[1], 1);
        axi_read(R_LEVEL, l1);  expect("full FIFO", l1, 64);
        axi_read(R_DROPPED, d1);
        repeat (6 * 32 * 2 * HALF) @(posedge clk);
        axi_read(R_LEVEL, l2);  expect("frozen FIFO", l2, 64);
        axi_read(R_DROPPED, v);
        if (v < d1 + 5) begin
            errors = errors + 1;
            $display("FAIL dropped did not grow while frozen: %0d then %0d", d1, v);
        end
        poll;                    // the 64 words before the gap, then OVERFLOW cleared
        expect("no jump before the gap", gaps, 0);
        axi_read(R_STATUS, v);  expect("overflow cleared", v[1], 0);
        axi_read(R_DROPPED, v); expect("dropped cleared", v, 0);
        run_until(nwords - 40);  // the words after the gap keep their indices
        expect("one gap", gaps, 1);
        axi_write(R_CTRL, 32'h101);  // CLEAR
        axi_read(R_LEVEL, v);   expect("level after CLEAR", v <= 1, 1);

        // ---- Kill: the clock stops and stays stopped until the next enable.
        kill = 1'b1;
        repeat (200) @(posedge clk);
        axi_read(R_STATUS, v);  expect("killed", v[2], 1); expect("not running", v[0], 0);
        repeat (300) @(posedge clk);
        expect("clock held low", mic_clk, 0);
        kill = 1'b0;
        repeat (200) @(posedge clk);
        axi_read(R_STATUS, v);  expect("still killed", v[2], 1);
        mic_on = 1'b0;
        axi_write(R_CTRL, 32'h0);

        // ---- L/R = VDD microphone read in the low half: the pull-up, all ones.
        axi_write(R_CTRL, 32'h1);
        mic_start(1'b1);
        next_idx = 0; gaps = 0; check_bits = 1'b0; ones_only = 1'b1; phase_off = 0;
        run_until(40);
        expect("wrong phase reads all ones", ones_only, 1);
        mic_on = 1'b0;
        axi_write(R_CTRL, 32'h0);

        // ---- L/R = VDD with PHASE = 1: the data again.
        axi_write(R_CTRL, 32'h3);
        mic_start(1'b1);
        next_idx = 0; gaps = 0; check_bits = 1'b1; ones_only = 1'b1; phase_off = HALF;
        run_until(300);
        expect("index jumps (PHASE = 1)", gaps, 0);
        expect("data, not the pull-up", ones_only, 0);

        if (errors == 0) $display("PASS pdm_mic: %0d words, overflow freeze and gap, kill, both phases", nwords);
        else $display("FAIL pdm_mic: %0d errors", errors);
        $finish;
    end
endmodule
