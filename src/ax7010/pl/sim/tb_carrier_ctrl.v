// Self-checking testbench for carrier_ctrl (Icarus Verilog):
//   iverilog -g2005 -o tb ../rtl/*.v IOBUF.v tb_carrier_ctrl.v && vvp tb
`timescale 1ns / 1ps
module tb_carrier_ctrl;
    reg clk = 0;
    always #5 clk = ~clk;  // 100 MHz
    reg aresetn = 0;

    reg  [11:0] awaddr = 0, araddr = 0;
    reg         awvalid = 0, wvalid = 0, bready = 1, arvalid = 0, rready = 1;
    reg  [31:0] wdata = 0;
    reg  [3:0] wstrb = 4'hF;
    wire        awready, wready, bvalid, arready, rvalid;
    wire [31:0] rdata;
    wire [1:0]  bresp, rresp;
    wire        irq, pfail_kill;

    reg k1 = 1, enc_sw = 1, enc_a = 1, enc_b = 1;
    reg tof_int = 0, rtc_1hz = 0, pfail_n = 1, exp_int_n = 1, tach_in = 0;
    reg esp_rxd = 1, esp_cts = 1, esp_irq = 0, prn_rxd = 1, prn_cts = 1;
    reg adxl_drdy = 0, imu_int1 = 0, imu_int2 = 0, spi_miso = 0;
    wire led1_pwm, led2_pwm, ws_data, esp_txd, esp_rts, prn_txd, adxl_cs, imu_cs, spi_sclk, spi_mosi;
    wire i2c0_scl, i2c0_sda, i2c1_scl, i2c1_sda;
    pullup (i2c0_scl);
    pullup (i2c0_sda);
    pullup (i2c1_scl);
    pullup (i2c1_sda);
    wire iic_scl_i0, iic_sda_i0, iic_scl_i1, iic_sda_i1;
    wire u_sin, u_ctsn, p_sin, p_ctsn, s_io1_i;

    carrier_ctrl #(.CLK_HZ(100000000)) dut (
        .s_axi_aclk(clk), .s_axi_aresetn(aresetn),
        .s_axi_awaddr(awaddr), .s_axi_awprot(3'd0), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(wstrb), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arprot(3'd0), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready),
        .irq(irq), .pfail_kill(pfail_kill),
        .led1_pwm(led1_pwm), .led2_pwm(led2_pwm), .k1(k1), .ws_data(ws_data), .tof_int(tof_int),
        .rtc_1hz(rtc_1hz), .pfail_n(pfail_n), .exp_int_n(exp_int_n),
        .i2c0_scl(i2c0_scl), .i2c0_sda(i2c0_sda), .i2c1_scl(i2c1_scl), .i2c1_sda(i2c1_sda),
        .tach_in(tach_in), .enc_sw(enc_sw), .enc_a(enc_a), .enc_b(enc_b),
        .esp_txd(esp_txd), .esp_rxd(esp_rxd), .esp_rts(esp_rts), .esp_cts(esp_cts), .esp_irq(esp_irq),
        .prn_txd(prn_txd), .prn_rxd(prn_rxd), .prn_cts(prn_cts),
        .adxl_cs(adxl_cs), .adxl_drdy(adxl_drdy), .imu_cs(imu_cs), .imu_int1(imu_int1), .imu_int2(imu_int2),
        .spi_miso(spi_miso), .spi_sclk(spi_sclk), .spi_mosi(spi_mosi),
        .iic0_scl_i(iic_scl_i0), .iic0_scl_o(1'b0), .iic0_scl_t(1'b1),
        .iic0_sda_i(iic_sda_i0), .iic0_sda_o(1'b0), .iic0_sda_t(1'b1),
        .iic1_scl_i(iic_scl_i1), .iic1_scl_o(1'b0), .iic1_scl_t(1'b1),
        .iic1_sda_i(iic_sda_i1), .iic1_sda_o(1'b0), .iic1_sda_t(1'b1),
        .uart_nano_sout(1'b1), .uart_nano_sin(u_sin), .uart_nano_rtsn(1'b0), .uart_nano_ctsn(u_ctsn),
        .uart_prn_sout(1'b1), .uart_prn_sin(p_sin), .uart_prn_ctsn(p_ctsn),
        .spi_sck_o(1'b0), .spi_io0_o(1'b0), .spi_io1_i(s_io1_i), .spi_ss_o(2'b11), .spi_ss_t(1'b0)
    );

    integer errors = 0;
    task check(input cond, input [8*64-1:0] what);
        begin
            if (cond !== 1'b1) begin
                errors = errors + 1;
                $display("FAIL: %0s (t=%0t)", what, $time);
            end
        end
    endtask

    task axi_write(input [11:0] a, input [31:0] d);
        begin
            @(posedge clk);
            awaddr <= a;
            wdata <= d;
            awvalid <= 1;
            wvalid <= 1;
            wait (awready && wready);
            @(posedge clk);
            awvalid <= 0;
            wvalid <= 0;
            wait (bvalid);
            @(posedge clk);
        end
    endtask

    task axi_read(input [11:0] a, output [31:0] d);
        begin
            @(posedge clk);
            araddr <= a;
            arvalid <= 1;
            wait (arready);
            @(posedge clk);
            arvalid <= 0;
            wait (rvalid);
            d = rdata;
            @(posedge clk);
        end
    endtask

    // Reads the head of the event FIFO and pops it.
    task pop_event(output [31:0] inf, output [63:0] ets);
        reg [31:0] elo, ehi;
        begin
            axi_read(12'h104, inf);
            axi_read(12'h108, elo);
            axi_read(12'h10C, ehi);
            ets = {ehi, elo};
            axi_write(12'h110, 1);
        end
    endtask

    // Captures one 120-bit frame from ws_data and counts the phases outside the
    // WS2812B-V6 limits (T0H 220-380 ns, T1H/T0L/T1L 580-1000 ns).
    time    t_rise, t_fall, t_first;
    integer ws_bad = 0;
    task ws_frame(output [119:0] frame);
        integer b;
        time    th;
        begin
            for (b = 119; b >= 0; b = b - 1) begin
                @(posedge ws_data);
                t_rise = $time;
                if (b == 119) t_first = t_rise;
                else if (t_rise - t_fall < 580 || t_rise - t_fall > 1000) ws_bad = ws_bad + 1;
                @(negedge ws_data);
                t_fall = $time;
                th = t_fall - t_rise;
                frame[b] = th > 475;
                if (frame[b] ? (th < 580 || th > 1000) : (th < 220 || th > 380)) ws_bad = ws_bad + 1;
            end
        end
    endtask

    initial begin
        #40000000;
        $fatal(1, "tb_carrier_ctrl: timeout");
    end

    reg [31:0]  v, info, info2, lo, hi;
    reg [63:0]  ets, ets2;
    integer     i, n, highs;
    reg [119:0] bits;
    time        t_end;

    initial begin
        repeat (10) @(posedge clk);
        aresetn = 1;
        repeat (5) @(posedge clk);

        // Identity and defaults.
        axi_read(12'h000, v);
        check(v == 32'h56424331, "ID");
        axi_read(12'h008, v);
        check(v == 100000000, "CLK_HZ");
        check(led1_pwm == 0 && led2_pwm == 0 && ws_data == 0, "outputs low after reset");
        check(imu_cs == 1 && adxl_cs == 1, "chip selects high");
        check(i2c0_scl === 1'b1 && iic_scl_i0 === 1'b1, "I2C released");

        // Independent AW/W handshakes and held responses under backpressure.
        @(negedge clk); bready = 0; awaddr = 12'h00C; awvalid = 1;
        @(posedge clk); @(negedge clk); awvalid = 0;
        repeat (5) @(posedge clk);
        @(negedge clk); wdata = 32'h12345678; wvalid = 1;
        @(posedge clk); @(negedge clk); wvalid = 0;
        wait (bvalid);
        repeat (5) begin @(negedge clk); check(bvalid === 1'b1, "BVALID held until BREADY"); end
        bready = 1; @(posedge clk); @(negedge clk);
        rready = 0; araddr = 12'h00C; arvalid = 1;
        @(posedge clk); @(negedge clk); arvalid = 0;
        wait (rvalid);
        repeat (5) begin
            @(negedge clk);
            check(rvalid === 1'b1 && rdata == 32'h12345678, "RDATA stable under backpressure");
        end
        rready = 1; @(posedge clk); @(negedge clk);

        wstrb = 4'b0010;
        axi_write(12'h00C, 32'hFFFFFF00);
        axi_read(12'h00C, v);
        check(v == 32'h1234FF78, "scratch byte strobe");
        wstrb = 0;
        axi_write(12'h040, 123);
        axi_read(12'h040, v);
        check(v == 5000, "zero strobe cannot change PWM period");
        wstrb = 4'hF;
        axi_write(12'h040, 24'h00ABCD);
        wstrb = 4'b0100;
        axi_write(12'h040, 24'h120000);
        axi_read(12'h040, v);
        check(v == 24'h12ABCD, "PWM period byte strobe");
        wstrb = 4'hF;
        axi_write(12'h040, 5000);

        // PWM 25 % on channel 0. New settings start with the next period, so
        // first wait out the reset default (CLK_HZ / 20000 = 5000 cycles).
        axi_write(12'h040, 100);
        axi_write(12'h044, 25);
        axi_write(12'h04C, 32'h31);
        repeat (5100) @(posedge clk);
        highs = 0;
        for (i = 0; i < 1000; i = i + 1) begin
            @(posedge clk);
            highs = highs + led1_pwm;
        end
        check(highs == 250, "PWM duty 25%");
        check(led2_pwm == 0, "PWM channel 1 disabled");

        // Power fail forces the lights off in hardware and stops the microphone clock.
        check(pfail_kill == 0, "no microphone kill without PFAIL");
        pfail_n = 0;
        repeat (20) @(posedge clk);
        check(led1_pwm == 0, "PFAIL kills PWM");
        check(pfail_kill == 1, "PFAIL kills the microphone clock");
        axi_read(12'h050, v);
        check(v[4] == 1, "PFAIL kill flag");
        pfail_n = 1;
        repeat (20) @(posedge clk);
        check(led1_pwm == 0, "kill stays until cleared");
        check(pfail_kill == 0, "microphone kill follows PFAIL");
        axi_write(12'h050, 32'h10);
        repeat (300) @(posedge clk);
        highs = 0;
        for (i = 0; i < 1000; i = i + 1) begin
            @(posedge clk);
            highs = highs + led1_pwm;
        end
        check(highs == 250, "PWM back after W1C");
        axi_write(12'h04C, 32'h30);

        // 64-bit timestamp: reading TS_LO latches the matching TS_HI, so a
        // carry between the two reads does not tear the value.
        @(posedge clk);
        #1 dut.ts = 64'h0000_0001_FFFF_FFF4;
        axi_read(12'h010, lo);
        axi_read(12'h014, hi);
        check(hi == 1 && lo >= 32'hFFFF_FFF4 && dut.ts[63:32] == 2, "TS_HI latched with TS_LO");
        axi_read(12'h010, lo);
        axi_read(12'h014, hi);
        check(hi == 2 && lo < 64, "TS carry into TS_HI");

        // Timestamped events: IMU_INT1 rising.
        axi_write(12'h120, 32'h0005_0005);  // rise: IMU_INT1, ADXL_DRDY, K1, K1_LONG
        axi_write(12'h124, 32'h0001_0000);  // fall: K1
        axi_write(12'h128, 1);
        imu_int1 = 1;
        repeat (30) @(posedge clk);
        imu_int1 = 0;
        repeat (10) @(posedge clk);
        check(irq == 1, "IRQ on pending event");
        axi_read(12'h100, v);
        check(v[9:0] == 1 && v[31] == 1, "one event queued");
        pop_event(info, ets);
        check(info[4:0] == 0 && info[8] == 1, "IMU_INT1 rise event");
        check(ets[63:32] == 2, "event carries the 64-bit timestamp");
        axi_read(12'h100, v);
        check(v[9:0] == 0, "event popped");
        check(irq == 0, "IRQ released");

        // Simultaneous edges keep one timestamp; the lower source id comes first.
        imu_int1 = 1;
        adxl_drdy = 1;
        repeat (30) @(posedge clk);
        imu_int1 = 0;
        adxl_drdy = 0;
        repeat (10) @(posedge clk);
        axi_read(12'h100, v);
        check(v[9:0] == 2, "two simultaneous events");
        pop_event(info, ets);
        pop_event(info2, ets2);
        check(info[4:0] == 0 && info2[4:0] == 2, "lower source first");
        check(ets == ets2, "same timestamp");
        check(info2[31:16] == info[31:16] + 16'd1, "consecutive sequence numbers");

        // K1: 1 ms debounce, long press after 3 ms -> press, long, release.
        axi_write(12'h030, 1);
        axi_write(12'h034, 3);
        k1 = 0;
        #1500000;
        axi_read(12'h024, v);
        check(v[0] == 1, "K1 pressed after debounce");
        #3000000;
        k1 = 1;
        #1500000;
        axi_read(12'h100, v);
        check(v[9:0] == 3, "K1 press, long, release");
        pop_event(info, ets);
        check(info[4:0] == 16 && info[8] == 1, "K1 press event");
        pop_event(info, ets2);
        check(info[4:0] == 18 && info[8] == 1, "K1 long event");
        check(ets2 - ets >= 200000 && ets2 - ets <= 300100, "long press after LONG_MS");
        pop_event(info, ets);
        check(info[4:0] == 16 && info[8] == 0, "K1 release event");

        // Encoder: a forward quadrature cycle is +4 with one step event each,
        // backwards is -4; a double transition only counts as an error.
        axi_write(12'h038, 2);              // 2 us filter
        axi_write(12'h120, 32'h0010_0000);  // rise: ENC_STEP
        enc_a = 0; #20000; enc_b = 0; #20000; enc_a = 1; #20000; enc_b = 1; #20000;
        axi_read(12'h028, v);
        check(v == 4, "encoder +4");
        axi_read(12'h100, v);
        check(v[9:0] == 4, "four step events");
        n = 0;
        for (i = 0; i < 4; i = i + 1) begin
            pop_event(info, ets);
            if (info[4:0] == 20 && info[8] == 1) n = n + 1;
        end
        check(n == 4, "step events count up");
        enc_b = 0; #20000; enc_a = 0; #20000; enc_b = 1; #20000; enc_a = 1; #20000;
        axi_read(12'h028, v);
        check(v == 0, "encoder back to 0");
        axi_read(12'h100, v);
        check(v[9:0] == 4, "four more step events");
        n = 0;
        for (i = 0; i < 4; i = i + 1) begin
            pop_event(info, ets);
            if (info[4:0] == 20 && info[8] == 0) n = n + 1;
        end
        check(n == 4, "step events count down");
        enc_a = 0; enc_b = 0; #20000;
        enc_a = 1; enc_b = 1; #20000;
        axi_read(12'h02C, v);
        check(v == 2, "double transitions are errors");
        axi_read(12'h028, v);
        check(v == 0, "errors do not move the count");
        axi_read(12'h100, v);
        check(v[9:0] == 0, "no step events for errors");

        // FIFO overflow: 520 rising edges into the 512-entry queue.
        axi_write(12'h120, 32'h0000_0001);
        for (i = 0; i < 520; i = i + 1) begin
            imu_int1 = 1;
            repeat (8) @(posedge clk);
            imu_int1 = 0;
            repeat (8) @(posedge clk);
        end
        repeat (10) @(posedge clk);
        axi_read(12'h100, v);
        check(v[9:0] == 512 && v[16] == 1 && v[31] == 1, "FIFO full with overflow flag");
        axi_read(12'h114, v);
        check(v == 8, "8 events dropped");
        pop_event(info, ets);
        pop_event(info2, ets2);
        check(info2[31:16] == info[31:16] + 16'd1 && ets2 > ets, "FIFO order kept");
        axi_write(12'h118, 1);
        axi_read(12'h100, v);
        check(v == 0, "clear empties the FIFO and the flag");
        axi_read(12'h114, v);
        check(v == 0, "clear resets DROPPED");
        check(irq == 0, "IRQ released after clear");

        // WS2812: two frames back to back (the second waits as pending). Every
        // phase must meet the V6 limits and the line must stay low for at
        // least WS_RESET_US between the frames.
        axi_write(12'h088, 100);
        axi_read(12'h088, v);
        check(v == 300, "WS_RESET_US clamped to 300");
        axi_write(12'h090, 24'hFF0000);  // HL101 red  -> G=00 R=FF B=00
        axi_write(12'h094, 24'h00FF00);
        axi_write(12'h098, 24'h0000FF);
        axi_write(12'h09C, 24'h123456);
        axi_write(12'h0A0, 24'h000001);
        fork
          begin
            axi_write(12'h080, 1);
            axi_write(12'h080, 1);
          end
          begin
        ws_frame(bits);
        check(bits[119:96] == 24'h00FF00, "LED0 GRB");
        check(bits[95:72] == 24'hFF0000, "LED1 GRB");
        check(bits[71:48] == 24'h0000FF, "LED2 GRB");
        check(bits[47:24] == 24'h341256, "LED3 GRB");
        check(bits[23:0] == 24'h000001, "LED4 GRB");
        t_end = t_fall;
        ws_frame(bits);
        check(t_first - t_end >= 300000, "reset low time >= WS_RESET_US");
        check(ws_bad == 0, "WS2812 phases within V6 limits");
          end
        join
        #400000;
        axi_read(12'h084, v);
        check(v[1:0] == 0 && v[31:16] == 2, "two frames sent, idle");

        // Watchdog: kicks keep it alive; without kicks it sends the fault
        // frame (HL101 red) and kills the PWM.
        axi_write(12'h04C, 32'h31);
        axi_write(12'h060, 2);
        for (i = 0; i < 5; i = i + 1) begin
            #1000000;
            axi_write(12'h064, 1);
        end
        axi_read(12'h068, v);
        check(v[0] == 0 && v[1] == 1, "kicked watchdog stays alive");
        ws_frame(bits);
        check(bits == {24'h004000, 96'd0}, "fault frame: HL101 red");
        axi_read(12'h068, v);
        check(v[0] == 1, "watchdog expired");
        axi_read(12'h050, v);
        check(v[5] == 1 && led1_pwm == 0, "watchdog kills PWM");
        axi_read(12'h084, v);
        check(v[2] == 1, "fault frame selected");
        axi_write(12'h064, 1);
        axi_write(12'h050, 32'h20);
        axi_read(12'h050, v);
        check(v[5] == 0, "kick + W1C clears the watchdog kill");
        axi_write(12'h080, 1);
        axi_read(12'h084, v);
        check(v[2] == 0, "SEND returns to the normal colors");

        if (errors == 0) $display("tb_carrier_ctrl: all checks passed");
        else $fatal(1, "tb_carrier_ctrl: %0d check(s) failed", errors);
        $finish;
    end
endmodule
