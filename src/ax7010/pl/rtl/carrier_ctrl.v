// Carrier board controller for AX7010 J11 (Bank 35), one AXI4-Lite slave.
//
//  * owns the carrier pins with the exact port names of
//    AX7010_carrier_V1.10.xdc (I2C pads are IOBUFs inside this module), all
//    but mic_clk/mic_dat on J706, which belong to ../audio/rtl/pdm_mic.v;
//  * passes the AXI IIC x2, AXI UART16550 x2 and AXI Quad SPI signals through;
//  * implements: front panel keys/encoder, two light PWM channels with
//    power-fail and watchdog kill, WS2812B-V6 driver for HL101..HL105,
//    a 64-bit timestamp counter and a timestamped event FIFO with interrupt;
//    pfail_kill stops the microphone clock while PFAIL is low.
//
// Register map: see ../README.md (also mirrored in linux/vibmon/src/hw/carrier_regs.h).
`timescale 1ns / 1ps
module carrier_ctrl #(
    parameter CLK_HZ = 100000000
) (
    // AXI4-Lite slave (4 KiB)
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
    (* X_INTERFACE_INFO = "xilinx.com:signal:interrupt:1.0 irq INTERRUPT" *)
    (* X_INTERFACE_PARAMETER = "SENSITIVITY LEVEL_HIGH" *)
    output wire        irq,
    output wire        pfail_kill,  // filtered PFAIL low while its kill is enabled (to pdm_mic kill)

    // ---- carrier pins (names match the XDC) ----
    output wire led1_pwm,
    output wire led2_pwm,
    input  wire k1,
    output wire ws_data,
    input  wire tof_int,
    input  wire rtc_1hz,
    input  wire pfail_n,
    input  wire exp_int_n,
    inout  wire i2c0_scl,
    inout  wire i2c0_sda,
    inout  wire i2c1_scl,
    inout  wire i2c1_sda,
    input  wire tach_in,
    input  wire enc_sw,
    input  wire enc_a,
    input  wire enc_b,
    output wire esp_txd,
    input  wire esp_rxd,
    output wire esp_rts,
    input  wire esp_cts,
    input  wire esp_irq,
    output wire prn_txd,
    input  wire prn_rxd,
    input  wire prn_cts,
    output wire adxl_cs,
    input  wire adxl_drdy,
    output wire imu_cs,
    input  wire imu_int1,
    input  wire imu_int2,
    input  wire spi_miso,
    output wire spi_sclk,
    output wire spi_mosi,

    // ---- AXI IIC x2 (management, sensors) ----
    output wire iic0_scl_i, input wire iic0_scl_o, input wire iic0_scl_t,
    output wire iic0_sda_i, input wire iic0_sda_o, input wire iic0_sda_t,
    output wire iic1_scl_i, input wire iic1_scl_o, input wire iic1_scl_t,
    output wire iic1_sda_i, input wire iic1_sda_o, input wire iic1_sda_t,

    // ---- AXI UART16550: Nano link (RTS/CTS) and printer (CTS) ----
    input  wire uart_nano_sout,
    output wire uart_nano_sin,
    input  wire uart_nano_rtsn,
    output wire uart_nano_ctsn,
    input  wire uart_prn_sout,
    output wire uart_prn_sin,
    output wire uart_prn_ctsn,

    // ---- AXI Quad SPI (standard mode, 2 slave selects: 0 = IMU, 1 = ADXL345) ----
    input  wire       spi_sck_o,
    input  wire       spi_io0_o,
    output wire       spi_io1_i,
    input  wire [1:0] spi_ss_o,
    input  wire       spi_ss_t
);
    localparam [31:0] ID_VALUE = 32'h5642_4331;  // "VBC1"
    localparam [31:0] VERSION  = 32'h0001_0000;
    localparam integer US_DIV = (CLK_HZ + 500000) / 1000000;
    localparam [23:0] PWM_DEFAULT_PERIOD = CLK_HZ / 20000;  // 20 kHz
    localparam NSRC = 21;

    wire clk = s_axi_aclk;
    wire rst = ~s_axi_aresetn;

    // ------------------------------------------------------------------
    // Pass-through to the Xilinx IP cores
    // ------------------------------------------------------------------
    IOBUF iob_i2c0_scl (.O(iic0_scl_i), .IO(i2c0_scl), .I(iic0_scl_o), .T(iic0_scl_t));
    IOBUF iob_i2c0_sda (.O(iic0_sda_i), .IO(i2c0_sda), .I(iic0_sda_o), .T(iic0_sda_t));
    IOBUF iob_i2c1_scl (.O(iic1_scl_i), .IO(i2c1_scl), .I(iic1_scl_o), .T(iic1_scl_t));
    IOBUF iob_i2c1_sda (.O(iic1_sda_i), .IO(i2c1_sda), .I(iic1_sda_o), .T(iic1_sda_t));

    wire esp_rxd_s, prn_rxd_s;
    cc_sync #(.WIDTH(2), .INIT(2'b11)) u_sync_rx (.clk(clk), .d({esp_rxd, prn_rxd}), .q({esp_rxd_s, prn_rxd_s}));

    assign esp_txd        = uart_nano_sout;
    assign uart_nano_sin  = esp_rxd_s;
    assign esp_rts        = uart_nano_rtsn;   // low = AX may receive
    assign prn_txd        = uart_prn_sout;
    assign uart_prn_sin   = prn_rxd_s;

    // While the SPI core tri-states its selects (disabled/reset) keep both chips deselected.
    assign imu_cs    = spi_ss_t ? 1'b1 : spi_ss_o[0];
    assign adxl_cs   = spi_ss_t ? 1'b1 : spi_ss_o[1];
    assign spi_sclk  = spi_sck_o;
    assign spi_mosi  = spi_io0_o;
    assign spi_io1_i = spi_miso;

    // ------------------------------------------------------------------
    // Input synchronizers and filters
    // ------------------------------------------------------------------
    // bit: 0 imu_int1 1 imu_int2 2 adxl_drdy 3 tof_int 4 rtc_1hz 5 tach_in
    //      6 pfail_n 7 exp_int_n 8 esp_irq 9 prn_cts 10 esp_cts
    wire [10:0] gen_raw = {esp_cts, prn_cts, esp_irq, exp_int_n, pfail_n, tach_in, rtc_1hz, tof_int, adxl_drdy, imu_int2, imu_int1};
    wire [10:0] gen_s, gen_f;
    // Idle levels: esp_cts/prn_cts high (busy), esp_irq low, exp_int_n/pfail_n high.
    cc_sync #(.WIDTH(11), .INIT(11'b110_1100_0000)) u_sync_gen (.clk(clk), .d(gen_raw), .q(gen_s));
    cc_glitch #(.WIDTH(11), .STABLE(4), .INIT(11'b110_1100_0000)) u_glitch_gen (.clk(clk), .d(gen_s), .q(gen_f));
    assign uart_nano_ctsn = gen_s[10];  // Nano RTS, low = Nano may receive
    assign uart_prn_ctsn  = gen_s[9];   // printer CTS, high = busy

    wire [3:0] pnl_s;  // k1, enc_sw, enc_a, enc_b
    cc_sync #(.WIDTH(4), .INIT(4'b1111)) u_sync_pnl (.clk(clk), .d({enc_b, enc_a, enc_sw, k1}), .q(pnl_s));

    // ------------------------------------------------------------------
    // Time base
    // ------------------------------------------------------------------
    reg [63:0] ts = 64'd0;
    reg [15:0] us_cnt = 16'd0;
    reg [9:0]  ms_cnt = 10'd0;
    reg        tick_us = 1'b0, tick_ms = 1'b0;
    always @(posedge clk) begin
        ts <= ts + 64'd1;
        tick_us <= 1'b0;
        tick_ms <= 1'b0;
        if (us_cnt == US_DIV - 1) begin
            us_cnt  <= 16'd0;
            tick_us <= 1'b1;
            if (ms_cnt == 10'd999) begin
                ms_cnt  <= 10'd0;
                tick_ms <= 1'b1;
            end else begin
                ms_cnt <= ms_cnt + 10'd1;
            end
        end else begin
            us_cnt <= us_cnt + 16'd1;
        end
    end

    // ------------------------------------------------------------------
    // Registers
    // ------------------------------------------------------------------
    reg [31:0] scratch = 32'd0;
    reg [7:0]  debounce_ms = 8'd15;
    reg [15:0] long_ms = 16'd3000;
    reg [9:0]  enc_filter_us = 10'd200;
    reg [23:0] pwm_period = PWM_DEFAULT_PERIOD;
    reg [23:0] pwm_duty0 = 24'd0, pwm_duty1 = 24'd0;
    reg        pwm_en0 = 1'b0, pwm_en1 = 1'b0;
    reg        pfail_kill_en = 1'b1, wdg_kill_en = 1'b1;
    reg        pfail_killed = 1'b0, wdg_killed = 1'b0;
    reg [15:0] wdg_timeout_ms = 16'd0;
    reg [15:0] wdg_cnt = 16'd0;
    reg        wdg_expired = 1'b0;
    reg [15:0] ws_reset_us = 16'd350;
    reg [23:0] ws_color [0:4];
    reg [23:0] ws_fault [0:4];
    reg        ws_use_fault = 1'b0;
    reg        ws_start = 1'b0;
    reg [31:0] evt_en_rise = 32'd0, evt_en_fall = 32'd0;
    reg        irq_en = 1'b0;
    reg        evt_pop = 1'b0, evt_clear = 1'b0;
    reg [31:0] ts_hi_latch = 32'd0;

    integer n;
    initial begin
        for (n = 0; n < 5; n = n + 1) begin
            ws_color[n] = 24'd0;
            ws_fault[n] = 24'd0;
        end
        ws_fault[0] = 24'h400000;  // HL101 red when the application stops kicking
    end

    // ------------------------------------------------------------------
    // Front panel
    // ------------------------------------------------------------------
    wire k1_pressed, k1_long, sw_pressed, sw_long;
    cc_key u_key_k1 (.clk(clk), .rst(rst), .tick_ms(tick_ms), .level(pnl_s[0]), .debounce_ms(debounce_ms),
                     .long_ms(long_ms), .pressed(k1_pressed), .long_pulse(k1_long));
    cc_key u_key_sw (.clk(clk), .rst(rst), .tick_ms(tick_ms), .level(pnl_s[1]), .debounce_ms(debounce_ms),
                     .long_ms(long_ms), .pressed(sw_pressed), .long_pulse(sw_long));
    wire signed [31:0] enc_count;
    wire [31:0] enc_errors;
    wire enc_step, enc_dir, enc_a_f, enc_b_f;
    cc_quad u_quad (.clk(clk), .rst(rst), .tick_us(tick_us), .a(pnl_s[2]), .b(pnl_s[3]), .filter_us(enc_filter_us),
                    .count(enc_count), .errors(enc_errors), .step(enc_step), .step_dir(enc_dir),
                    .a_filt(enc_a_f), .b_filt(enc_b_f));

    // ------------------------------------------------------------------
    // PWM with kill sources
    // ------------------------------------------------------------------
    wire pfail_low = ~gen_f[6];
    wire pwm_kill = pfail_killed | wdg_killed;
    assign pfail_kill = pfail_kill_en & pfail_low;
    cc_pwm u_pwm (.clk(clk), .rst(rst), .period(pwm_period), .duty0(pwm_duty0), .duty1(pwm_duty1),
                  .en0(pwm_en0), .en1(pwm_en1), .kill(pwm_kill), .out0(led1_pwm), .out1(led2_pwm));

    // ------------------------------------------------------------------
    // WS2812
    // ------------------------------------------------------------------
    function [23:0] rgb2grb(input [23:0] rgb);
        rgb2grb = {rgb[15:8], rgb[23:16], rgb[7:0]};
    endfunction
    wire [119:0] ws_grb_norm  = {rgb2grb(ws_color[4]), rgb2grb(ws_color[3]), rgb2grb(ws_color[2]),
                                 rgb2grb(ws_color[1]), rgb2grb(ws_color[0])};
    wire [119:0] ws_grb_fault = {rgb2grb(ws_fault[4]), rgb2grb(ws_fault[3]), rgb2grb(ws_fault[2]),
                                 rgb2grb(ws_fault[1]), rgb2grb(ws_fault[0])};
    wire ws_busy, ws_pending;
    wire [15:0] ws_frames;
    cc_ws2812 #(.CLK_HZ(CLK_HZ), .NLED(5)) u_ws (.clk(clk), .rst(rst), .start(ws_start),
        .grb(ws_use_fault ? ws_grb_fault : ws_grb_norm), .reset_us(ws_reset_us), .tick_us(tick_us),
        .dout(ws_data), .busy(ws_busy), .pending(ws_pending), .frames(ws_frames));

    // ------------------------------------------------------------------
    // Events
    // ------------------------------------------------------------------
    // Source ids 0..10 = gen bits, 16 K1 pressed, 17 ENC_SW pressed, 18 K1 long,
    // 19 ENC_SW long, 20 encoder step (level = direction, 1 = up).
    wire [NSRC-1:0] src_level = {enc_dir, sw_long, k1_long, sw_pressed, k1_pressed, 5'd0, gen_f};
    reg  [NSRC-1:0] src_prev = {NSRC{1'b0}};
    always @(posedge clk) src_prev <= src_level;
    wire [NSRC-1:0] rise = src_level & ~src_prev;
    wire [NSRC-1:0] fall = ~src_level & src_prev;
    wire [NSRC-1:0] hit_edges = (rise & evt_en_rise[NSRC-1:0]) | (fall & evt_en_fall[NSRC-1:0]);
    wire [NSRC-1:0] hit = {enc_step & evt_en_rise[20], hit_edges[19:0]};
    wire [9:0]  evt_count;
    wire        evt_overflow;
    wire [31:0] evt_dropped, evt_info;
    wire [63:0] evt_ts;
    cc_events #(.NSRC(NSRC), .DEPTH_LOG2(9)) u_events (.clk(clk), .rst(rst), .ts(ts), .hit(hit), .lvl(src_level),
        .pop(evt_pop), .clear(evt_clear), .count(evt_count), .overflow(evt_overflow), .dropped(evt_dropped),
        .head_info(evt_info), .head_ts(evt_ts));
    assign irq = irq_en & (evt_count != 0);

    // ------------------------------------------------------------------
    // AXI4-Lite write channel
    // ------------------------------------------------------------------
    reg        aw_hold = 1'b0, w_hold = 1'b0;
    reg [11:0] aw_addr_q = 12'd0;
    reg [31:0] w_data_q = 32'd0;
    reg [3:0]  w_strb_q = 4'd0;
    assign s_axi_awready = ~aw_hold & ~s_axi_bvalid;
    assign s_axi_wready  = ~w_hold & ~s_axi_bvalid;
    assign s_axi_bresp   = 2'b00;
    assign s_axi_rresp   = 2'b00;

    function [31:0] merge(input [31:0] old, input [31:0] data, input [3:0] strb);
        merge = {strb[3] ? data[31:24] : old[31:24], strb[2] ? data[23:16] : old[23:16],
                 strb[1] ? data[15:8] : old[15:8], strb[0] ? data[7:0] : old[7:0]};
    endfunction

    wire do_write = aw_hold & w_hold & ~s_axi_bvalid;
    wire [9:0] waddr = aw_addr_q[11:2];
    wire [31:0] wd = w_data_q;
    wire [31:0] wbits = merge(32'd0, wd, w_strb_q);
    function [15:0] clamp_reset(input [31:0] value);
        clamp_reset = value[15:0] < 16'd300 ? 16'd300 : value[15:0];
    endfunction

    always @(posedge clk) begin
        ws_start  <= 1'b0;
        evt_pop   <= 1'b0;
        evt_clear <= 1'b0;
        if (rst) begin
            aw_hold <= 1'b0;
            w_hold  <= 1'b0;
            s_axi_bvalid <= 1'b0;
            pwm_en0 <= 1'b0;
            pwm_en1 <= 1'b0;
            pwm_duty0 <= 24'd0;
            pwm_duty1 <= 24'd0;
            pfail_killed <= 1'b0;
            wdg_killed <= 1'b0;
            wdg_timeout_ms <= 16'd0;
            wdg_expired <= 1'b0;
            ws_use_fault <= 1'b0;
            evt_en_rise <= 32'd0;
            evt_en_fall <= 32'd0;
            irq_en <= 1'b0;
        end else begin
            if (s_axi_awvalid && s_axi_awready) begin
                aw_hold   <= 1'b1;
                aw_addr_q <= s_axi_awaddr;
            end
            if (s_axi_wvalid && s_axi_wready) begin
                w_hold   <= 1'b1;
                w_data_q <= s_axi_wdata;
                w_strb_q <= s_axi_wstrb;
            end
            if (s_axi_bvalid && s_axi_bready) s_axi_bvalid <= 1'b0;

            // Watchdog countdown.
            if (wdg_timeout_ms != 16'd0 && tick_ms && !wdg_expired) begin
                if (wdg_cnt == 16'd0) begin
                    wdg_expired <= 1'b1;
                    ws_use_fault <= 1'b1;
                    ws_start <= 1'b1;
                end else begin
                    wdg_cnt <= wdg_cnt - 16'd1;
                end
            end
            if (pfail_kill_en && pfail_low) pfail_killed <= 1'b1;
            if (wdg_kill_en && wdg_expired) wdg_killed <= 1'b1;

            if (do_write) begin
                aw_hold <= 1'b0;
                w_hold  <= 1'b0;
                s_axi_bvalid <= 1'b1;
                if (|w_strb_q) case (waddr)
                    10'h003: scratch <= merge(scratch, wd, w_strb_q);
                    10'h00C: debounce_ms <= merge(debounce_ms, wd, w_strb_q);
                    10'h00D: long_ms <= merge(long_ms, wd, w_strb_q);
                    10'h00E: enc_filter_us <= merge(enc_filter_us, wd, w_strb_q);
                    10'h010: pwm_period <= merge(pwm_period, wd, w_strb_q);
                    10'h011: pwm_duty0 <= merge(pwm_duty0, wd, w_strb_q);
                    10'h012: pwm_duty1 <= merge(pwm_duty1, wd, w_strb_q);
                    10'h013: begin
                        if (w_strb_q[0]) pwm_en0 <= wd[0];
                        if (w_strb_q[0]) pwm_en1 <= wd[1];
                        if (w_strb_q[0]) pfail_kill_en <= wd[4];
                        if (w_strb_q[0]) wdg_kill_en <= wd[5];
                    end
                    10'h014: begin  // PWM_STATUS: write 1 to clear a kill once its cause is gone
                        if (wbits[4] && !pfail_low) pfail_killed <= 1'b0;
                        if (wbits[5] && !wdg_expired) wdg_killed <= 1'b0;
                    end
                    10'h018: begin
                        wdg_timeout_ms <= merge(wdg_timeout_ms, wd, w_strb_q);
                        wdg_cnt <= merge(wdg_cnt, wd, w_strb_q);
                        wdg_expired <= 1'b0;
                    end
                    10'h019: begin  // WDG_KICK
                        wdg_cnt <= wdg_timeout_ms;
                        wdg_expired <= 1'b0;
                    end
                    10'h020: if (wbits[0]) begin  // WS_CTRL.SEND
                        ws_use_fault <= 1'b0;
                        ws_start <= 1'b1;
                    end
                    10'h022: ws_reset_us <= clamp_reset(merge(ws_reset_us, wd, w_strb_q));
                    10'h024: ws_color[0] <= merge(ws_color[0], wd, w_strb_q);
                    10'h025: ws_color[1] <= merge(ws_color[1], wd, w_strb_q);
                    10'h026: ws_color[2] <= merge(ws_color[2], wd, w_strb_q);
                    10'h027: ws_color[3] <= merge(ws_color[3], wd, w_strb_q);
                    10'h028: ws_color[4] <= merge(ws_color[4], wd, w_strb_q);
                    10'h02C: ws_fault[0] <= merge(ws_fault[0], wd, w_strb_q);
                    10'h02D: ws_fault[1] <= merge(ws_fault[1], wd, w_strb_q);
                    10'h02E: ws_fault[2] <= merge(ws_fault[2], wd, w_strb_q);
                    10'h02F: ws_fault[3] <= merge(ws_fault[3], wd, w_strb_q);
                    10'h030: ws_fault[4] <= merge(ws_fault[4], wd, w_strb_q);
                    10'h044: if (w_strb_q[0]) evt_pop <= wbits[0];
                    10'h046: if (w_strb_q[0]) evt_clear <= wbits[0];
                    10'h048: evt_en_rise <= merge(evt_en_rise, wd, w_strb_q);
                    10'h049: evt_en_fall <= merge(evt_en_fall, wd, w_strb_q);
                    10'h04A: if (w_strb_q[0]) irq_en <= wbits[0];
                    default: ;
                endcase
            end
        end
    end

    // ------------------------------------------------------------------
    // AXI4-Lite read channel (fixed 4-cycle latency so the event FIFO head
    // is always current after a POP)
    // ------------------------------------------------------------------
    reg        rd_busy = 1'b0;
    reg [2:0]  rd_wait = 3'd0;
    reg [11:0] ar_addr_q = 12'd0;
    assign s_axi_arready = ~rd_busy & ~s_axi_rvalid;

    wire [31:0] in_raw = {17'd0, enc_b_f, enc_a_f, pnl_s[1], pnl_s[0], gen_f};
    reg [31:0] rmux;
    always @* begin
        case (ar_addr_q[11:2])
            10'h000: rmux = ID_VALUE;
            10'h001: rmux = VERSION;
            10'h002: rmux = CLK_HZ;
            10'h003: rmux = scratch;
            10'h004: rmux = ts[31:0];
            10'h005: rmux = ts_hi_latch;
            10'h008: rmux = in_raw;
            10'h009: rmux = {30'd0, sw_pressed, k1_pressed};
            10'h00A: rmux = enc_count;
            10'h00B: rmux = enc_errors;
            10'h00C: rmux = {24'd0, debounce_ms};
            10'h00D: rmux = {16'd0, long_ms};
            10'h00E: rmux = {22'd0, enc_filter_us};
            10'h010: rmux = {8'd0, pwm_period};
            10'h011: rmux = {8'd0, pwm_duty0};
            10'h012: rmux = {8'd0, pwm_duty1};
            10'h013: rmux = {26'd0, wdg_kill_en, pfail_kill_en, 2'd0, pwm_en1, pwm_en0};
            10'h014: rmux = {26'd0, wdg_killed, pfail_killed, 2'd0, led2_pwm, led1_pwm};
            10'h018: rmux = {16'd0, wdg_timeout_ms};
            10'h01A: rmux = {14'd0, wdg_cnt, (wdg_timeout_ms != 16'd0), wdg_expired};
            10'h021: rmux = {ws_frames, 13'd0, ws_use_fault, ws_pending, ws_busy};
            10'h022: rmux = {16'd0, ws_reset_us};
            10'h024: rmux = {8'd0, ws_color[0]};
            10'h025: rmux = {8'd0, ws_color[1]};
            10'h026: rmux = {8'd0, ws_color[2]};
            10'h027: rmux = {8'd0, ws_color[3]};
            10'h028: rmux = {8'd0, ws_color[4]};
            10'h02C: rmux = {8'd0, ws_fault[0]};
            10'h02D: rmux = {8'd0, ws_fault[1]};
            10'h02E: rmux = {8'd0, ws_fault[2]};
            10'h02F: rmux = {8'd0, ws_fault[3]};
            10'h030: rmux = {8'd0, ws_fault[4]};
            10'h040: rmux = {(evt_count != 0), 14'd0, evt_overflow, 6'd0, evt_count};
            10'h041: rmux = evt_info;
            10'h042: rmux = evt_ts[31:0];
            10'h043: rmux = evt_ts[63:32];
            10'h045: rmux = evt_dropped;
            10'h048: rmux = evt_en_rise;
            10'h049: rmux = evt_en_fall;
            10'h04A: rmux = {31'd0, irq_en};
            10'h04B: rmux = {31'd0, irq};
            default: rmux = 32'd0;
        endcase
    end

    always @(posedge clk) begin
        if (rst) begin
            rd_busy <= 1'b0;
            s_axi_rvalid <= 1'b0;
        end else begin
            if (s_axi_arvalid && s_axi_arready) begin
                rd_busy   <= 1'b1;
                rd_wait   <= 3'd3;
                ar_addr_q <= s_axi_araddr;
            end else if (rd_busy) begin
                if (rd_wait == 3'd0) begin
                    rd_busy <= 1'b0;
                    s_axi_rvalid <= 1'b1;
                    s_axi_rdata <= rmux;
                    if (ar_addr_q[11:2] == 10'h004) ts_hi_latch <= ts[63:32];  // TS_LO read latches TS_HI
                end else begin
                    rd_wait <= rd_wait - 3'd1;
                end
            end
            if (s_axi_rvalid && s_axi_rready) s_axi_rvalid <= 1'b0;
        end
    end
endmodule
