## Carrier pins are asynchronous to the fabric clock: every input is
## synchronized in carrier_ctrl or pdm_mic (two-stage synchronizers), outputs
## are slow control lines (PWM, WS2812, chip selects, UART) and the 2 MHz
## microphone clock, whose data is taken SAMPLE_AT system clocks after the
## edge (a register setting, not a timing path). No I/O timing paths are analysed.
set_false_path -from [get_ports {k1 tof_int rtc_1hz pfail_n exp_int_n tach_in enc_sw enc_a enc_b esp_rxd esp_cts esp_irq prn_rxd prn_cts adxl_drdy imu_int1 imu_int2 spi_miso}]
set_false_path -to [get_ports {led1_pwm led2_pwm ws_data esp_txd esp_rts prn_txd adxl_cs imu_cs spi_sclk spi_mosi}]
set_false_path -from [get_ports {i2c0_scl i2c0_sda i2c1_scl i2c1_sda}]
set_false_path -to [get_ports {i2c0_scl i2c0_sda i2c1_scl i2c1_sda}]
set_false_path -to [get_ports mic_clk]
set_false_path -from [get_ports mic_dat]
