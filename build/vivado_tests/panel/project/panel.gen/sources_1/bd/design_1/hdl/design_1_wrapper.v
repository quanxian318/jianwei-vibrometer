//Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
//Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
//--------------------------------------------------------------------------------
//Tool Version: Vivado v.2025.2 (win64) Build 6299465 Fri Nov 14 19:35:11 GMT 2025
//Date        : Sat Oct 31 10:51:44 2026
//Host        : LAPTOP-T5AMSP3P running 64-bit major release  (build 9200)
//Command     : generate_target design_1_wrapper.bd
//Design      : design_1_wrapper
//Purpose     : IP block netlist
//--------------------------------------------------------------------------------
`timescale 1 ps / 1 ps

module design_1_wrapper
   (DDR_addr,
    DDR_ba,
    DDR_cas_n,
    DDR_ck_n,
    DDR_ck_p,
    DDR_cke,
    DDR_cs_n,
    DDR_dm,
    DDR_dq,
    DDR_dqs_n,
    DDR_dqs_p,
    DDR_odt,
    DDR_ras_n,
    DDR_reset_n,
    DDR_we_n,
    FIXED_IO_ddr_vrn,
    FIXED_IO_ddr_vrp,
    FIXED_IO_mio,
    FIXED_IO_ps_clk,
    FIXED_IO_ps_porb,
    FIXED_IO_ps_srstb,
    TMDS_clk_n,
    TMDS_clk_p,
    TMDS_data_n,
    TMDS_data_p,
    carrier_mgmt_i2c_scl_io,
    carrier_mgmt_i2c_sda_io,
    carrier_sens_i2c_scl_io,
    carrier_sens_i2c_sda_io,
    cmos1_d,
    cmos1_href,
    cmos1_i2c_scl_io,
    cmos1_i2c_sda_io,
    cmos1_pclk,
    cmos1_vsync,
    cmos2_d,
    cmos2_href,
    cmos2_i2c_scl_io,
    cmos2_i2c_sda_io,
    cmos2_pclk,
    cmos2_vsync,
    cmos_rstn_tri_o,
    enc_a,
    enc_b,
    enc_sw,
    hdmi_oen,
    k1,
    lamp_pfail_n,
    led1_pwm,
    led2_pwm,
    mic_clk,
    mic_dat,
    prn_cts,
    prn_rxd,
    prn_txd,
    ws_data);
  inout [14:0]DDR_addr;
  inout [2:0]DDR_ba;
  inout DDR_cas_n;
  inout DDR_ck_n;
  inout DDR_ck_p;
  inout DDR_cke;
  inout DDR_cs_n;
  inout [3:0]DDR_dm;
  inout [31:0]DDR_dq;
  inout [3:0]DDR_dqs_n;
  inout [3:0]DDR_dqs_p;
  inout DDR_odt;
  inout DDR_ras_n;
  inout DDR_reset_n;
  inout DDR_we_n;
  inout FIXED_IO_ddr_vrn;
  inout FIXED_IO_ddr_vrp;
  inout [53:0]FIXED_IO_mio;
  inout FIXED_IO_ps_clk;
  inout FIXED_IO_ps_porb;
  inout FIXED_IO_ps_srstb;
  output TMDS_clk_n;
  output TMDS_clk_p;
  output [2:0]TMDS_data_n;
  output [2:0]TMDS_data_p;
  inout carrier_mgmt_i2c_scl_io;
  inout carrier_mgmt_i2c_sda_io;
  inout carrier_sens_i2c_scl_io;
  inout carrier_sens_i2c_sda_io;
  input [9:0]cmos1_d;
  input cmos1_href;
  inout cmos1_i2c_scl_io;
  inout cmos1_i2c_sda_io;
  input cmos1_pclk;
  input cmos1_vsync;
  input [9:0]cmos2_d;
  input cmos2_href;
  inout cmos2_i2c_scl_io;
  inout cmos2_i2c_sda_io;
  input cmos2_pclk;
  input cmos2_vsync;
  output [1:0]cmos_rstn_tri_o;
  input enc_a;
  input enc_b;
  input enc_sw;
  output hdmi_oen;
  input k1;
  input lamp_pfail_n;
  output led1_pwm;
  output led2_pwm;
  output mic_clk;
  input mic_dat;
  input prn_cts;
  input prn_rxd;
  output prn_txd;
  output ws_data;

  wire [14:0]DDR_addr;
  wire [2:0]DDR_ba;
  wire DDR_cas_n;
  wire DDR_ck_n;
  wire DDR_ck_p;
  wire DDR_cke;
  wire DDR_cs_n;
  wire [3:0]DDR_dm;
  wire [31:0]DDR_dq;
  wire [3:0]DDR_dqs_n;
  wire [3:0]DDR_dqs_p;
  wire DDR_odt;
  wire DDR_ras_n;
  wire DDR_reset_n;
  wire DDR_we_n;
  wire FIXED_IO_ddr_vrn;
  wire FIXED_IO_ddr_vrp;
  wire [53:0]FIXED_IO_mio;
  wire FIXED_IO_ps_clk;
  wire FIXED_IO_ps_porb;
  wire FIXED_IO_ps_srstb;
  wire TMDS_clk_n;
  wire TMDS_clk_p;
  wire [2:0]TMDS_data_n;
  wire [2:0]TMDS_data_p;
  wire carrier_mgmt_i2c_scl_i;
  wire carrier_mgmt_i2c_scl_io;
  wire carrier_mgmt_i2c_scl_o;
  wire carrier_mgmt_i2c_scl_t;
  wire carrier_mgmt_i2c_sda_i;
  wire carrier_mgmt_i2c_sda_io;
  wire carrier_mgmt_i2c_sda_o;
  wire carrier_mgmt_i2c_sda_t;
  wire carrier_sens_i2c_scl_i;
  wire carrier_sens_i2c_scl_io;
  wire carrier_sens_i2c_scl_o;
  wire carrier_sens_i2c_scl_t;
  wire carrier_sens_i2c_sda_i;
  wire carrier_sens_i2c_sda_io;
  wire carrier_sens_i2c_sda_o;
  wire carrier_sens_i2c_sda_t;
  wire [9:0]cmos1_d;
  wire cmos1_href;
  wire cmos1_i2c_scl_i;
  wire cmos1_i2c_scl_io;
  wire cmos1_i2c_scl_o;
  wire cmos1_i2c_scl_t;
  wire cmos1_i2c_sda_i;
  wire cmos1_i2c_sda_io;
  wire cmos1_i2c_sda_o;
  wire cmos1_i2c_sda_t;
  wire cmos1_pclk;
  wire cmos1_vsync;
  wire [9:0]cmos2_d;
  wire cmos2_href;
  wire cmos2_i2c_scl_i;
  wire cmos2_i2c_scl_io;
  wire cmos2_i2c_scl_o;
  wire cmos2_i2c_scl_t;
  wire cmos2_i2c_sda_i;
  wire cmos2_i2c_sda_io;
  wire cmos2_i2c_sda_o;
  wire cmos2_i2c_sda_t;
  wire cmos2_pclk;
  wire cmos2_vsync;
  wire [1:0]cmos_rstn_tri_o;
  wire enc_a;
  wire enc_b;
  wire enc_sw;
  wire hdmi_oen;
  wire k1;
  wire lamp_pfail_n;
  wire led1_pwm;
  wire led2_pwm;
  wire mic_clk;
  wire mic_dat;
  wire prn_cts;
  wire prn_rxd;
  wire prn_txd;
  wire ws_data;

  IOBUF carrier_mgmt_i2c_scl_iobuf
       (.I(carrier_mgmt_i2c_scl_o),
        .IO(carrier_mgmt_i2c_scl_io),
        .O(carrier_mgmt_i2c_scl_i),
        .T(carrier_mgmt_i2c_scl_t));
  IOBUF carrier_mgmt_i2c_sda_iobuf
       (.I(carrier_mgmt_i2c_sda_o),
        .IO(carrier_mgmt_i2c_sda_io),
        .O(carrier_mgmt_i2c_sda_i),
        .T(carrier_mgmt_i2c_sda_t));
  IOBUF carrier_sens_i2c_scl_iobuf
       (.I(carrier_sens_i2c_scl_o),
        .IO(carrier_sens_i2c_scl_io),
        .O(carrier_sens_i2c_scl_i),
        .T(carrier_sens_i2c_scl_t));
  IOBUF carrier_sens_i2c_sda_iobuf
       (.I(carrier_sens_i2c_sda_o),
        .IO(carrier_sens_i2c_sda_io),
        .O(carrier_sens_i2c_sda_i),
        .T(carrier_sens_i2c_sda_t));
  IOBUF cmos1_i2c_scl_iobuf
       (.I(cmos1_i2c_scl_o),
        .IO(cmos1_i2c_scl_io),
        .O(cmos1_i2c_scl_i),
        .T(cmos1_i2c_scl_t));
  IOBUF cmos1_i2c_sda_iobuf
       (.I(cmos1_i2c_sda_o),
        .IO(cmos1_i2c_sda_io),
        .O(cmos1_i2c_sda_i),
        .T(cmos1_i2c_sda_t));
  IOBUF cmos2_i2c_scl_iobuf
       (.I(cmos2_i2c_scl_o),
        .IO(cmos2_i2c_scl_io),
        .O(cmos2_i2c_scl_i),
        .T(cmos2_i2c_scl_t));
  IOBUF cmos2_i2c_sda_iobuf
       (.I(cmos2_i2c_sda_o),
        .IO(cmos2_i2c_sda_io),
        .O(cmos2_i2c_sda_i),
        .T(cmos2_i2c_sda_t));
  design_1 design_1_i
       (.DDR_addr(DDR_addr),
        .DDR_ba(DDR_ba),
        .DDR_cas_n(DDR_cas_n),
        .DDR_ck_n(DDR_ck_n),
        .DDR_ck_p(DDR_ck_p),
        .DDR_cke(DDR_cke),
        .DDR_cs_n(DDR_cs_n),
        .DDR_dm(DDR_dm),
        .DDR_dq(DDR_dq),
        .DDR_dqs_n(DDR_dqs_n),
        .DDR_dqs_p(DDR_dqs_p),
        .DDR_odt(DDR_odt),
        .DDR_ras_n(DDR_ras_n),
        .DDR_reset_n(DDR_reset_n),
        .DDR_we_n(DDR_we_n),
        .FIXED_IO_ddr_vrn(FIXED_IO_ddr_vrn),
        .FIXED_IO_ddr_vrp(FIXED_IO_ddr_vrp),
        .FIXED_IO_mio(FIXED_IO_mio),
        .FIXED_IO_ps_clk(FIXED_IO_ps_clk),
        .FIXED_IO_ps_porb(FIXED_IO_ps_porb),
        .FIXED_IO_ps_srstb(FIXED_IO_ps_srstb),
        .TMDS_clk_n(TMDS_clk_n),
        .TMDS_clk_p(TMDS_clk_p),
        .TMDS_data_n(TMDS_data_n),
        .TMDS_data_p(TMDS_data_p),
        .carrier_mgmt_i2c_scl_i(carrier_mgmt_i2c_scl_i),
        .carrier_mgmt_i2c_scl_o(carrier_mgmt_i2c_scl_o),
        .carrier_mgmt_i2c_scl_t(carrier_mgmt_i2c_scl_t),
        .carrier_mgmt_i2c_sda_i(carrier_mgmt_i2c_sda_i),
        .carrier_mgmt_i2c_sda_o(carrier_mgmt_i2c_sda_o),
        .carrier_mgmt_i2c_sda_t(carrier_mgmt_i2c_sda_t),
        .carrier_sens_i2c_scl_i(carrier_sens_i2c_scl_i),
        .carrier_sens_i2c_scl_o(carrier_sens_i2c_scl_o),
        .carrier_sens_i2c_scl_t(carrier_sens_i2c_scl_t),
        .carrier_sens_i2c_sda_i(carrier_sens_i2c_sda_i),
        .carrier_sens_i2c_sda_o(carrier_sens_i2c_sda_o),
        .carrier_sens_i2c_sda_t(carrier_sens_i2c_sda_t),
        .cmos1_d(cmos1_d),
        .cmos1_href(cmos1_href),
        .cmos1_i2c_scl_i(cmos1_i2c_scl_i),
        .cmos1_i2c_scl_o(cmos1_i2c_scl_o),
        .cmos1_i2c_scl_t(cmos1_i2c_scl_t),
        .cmos1_i2c_sda_i(cmos1_i2c_sda_i),
        .cmos1_i2c_sda_o(cmos1_i2c_sda_o),
        .cmos1_i2c_sda_t(cmos1_i2c_sda_t),
        .cmos1_pclk(cmos1_pclk),
        .cmos1_vsync(cmos1_vsync),
        .cmos2_d(cmos2_d),
        .cmos2_href(cmos2_href),
        .cmos2_i2c_scl_i(cmos2_i2c_scl_i),
        .cmos2_i2c_scl_o(cmos2_i2c_scl_o),
        .cmos2_i2c_scl_t(cmos2_i2c_scl_t),
        .cmos2_i2c_sda_i(cmos2_i2c_sda_i),
        .cmos2_i2c_sda_o(cmos2_i2c_sda_o),
        .cmos2_i2c_sda_t(cmos2_i2c_sda_t),
        .cmos2_pclk(cmos2_pclk),
        .cmos2_vsync(cmos2_vsync),
        .cmos_rstn_tri_o(cmos_rstn_tri_o),
        .enc_a(enc_a),
        .enc_b(enc_b),
        .enc_sw(enc_sw),
        .hdmi_oen(hdmi_oen),
        .k1(k1),
        .lamp_pfail_n(lamp_pfail_n),
        .led1_pwm(led1_pwm),
        .led2_pwm(led2_pwm),
        .mic_clk(mic_clk),
        .mic_dat(mic_dat),
        .prn_cts(prn_cts),
        .prn_rxd(prn_rxd),
        .prn_txd(prn_txd),
        .ws_data(ws_data));
endmodule
