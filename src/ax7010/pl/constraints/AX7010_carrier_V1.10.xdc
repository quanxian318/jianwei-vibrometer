## 见微载板 V1.10 —— AX7010 J11（Bank 35，VCCIO 3.3V）；载板接口管脚分配
## 相机 AN5642 接 J10，其约束另行维护；合并时检查无重复占用

set_property -dict {PACKAGE_PIN F17 IOSTANDARD LVCMOS33} [get_ports led1_pwm]
set_property -dict {PACKAGE_PIN F16 IOSTANDARD LVCMOS33} [get_ports led2_pwm]
set_property -dict {PACKAGE_PIN F20 IOSTANDARD LVCMOS33} [get_ports k1]
set_property -dict {PACKAGE_PIN F19 IOSTANDARD LVCMOS33} [get_ports ws_data]
set_property -dict {PACKAGE_PIN G20 IOSTANDARD LVCMOS33} [get_ports tof_int]
set_property -dict {PACKAGE_PIN G19 IOSTANDARD LVCMOS33} [get_ports rtc_1hz]
set_property -dict {PACKAGE_PIN H18 IOSTANDARD LVCMOS33} [get_ports pfail_n]
set_property -dict {PACKAGE_PIN J18 IOSTANDARD LVCMOS33} [get_ports exp_int_n]
set_property -dict {PACKAGE_PIN L20 IOSTANDARD LVCMOS33} [get_ports i2c0_scl]
set_property -dict {PACKAGE_PIN L19 IOSTANDARD LVCMOS33} [get_ports i2c0_sda]
set_property -dict {PACKAGE_PIN M20 IOSTANDARD LVCMOS33} [get_ports i2c1_scl]
set_property -dict {PACKAGE_PIN M19 IOSTANDARD LVCMOS33} [get_ports i2c1_sda]
set_property -dict {PACKAGE_PIN K18 IOSTANDARD LVCMOS33} [get_ports tach_in]
set_property -dict {PACKAGE_PIN K17 IOSTANDARD LVCMOS33} [get_ports enc_sw]
set_property -dict {PACKAGE_PIN J19 IOSTANDARD LVCMOS33} [get_ports enc_a]
set_property -dict {PACKAGE_PIN K19 IOSTANDARD LVCMOS33} [get_ports enc_b]
set_property -dict {PACKAGE_PIN H20 IOSTANDARD LVCMOS33} [get_ports esp_txd]
set_property -dict {PACKAGE_PIN J20 IOSTANDARD LVCMOS33} [get_ports esp_rxd]
set_property -dict {PACKAGE_PIN L17 IOSTANDARD LVCMOS33} [get_ports esp_rts]
set_property -dict {PACKAGE_PIN L16 IOSTANDARD LVCMOS33} [get_ports esp_cts]
set_property -dict {PACKAGE_PIN M18 IOSTANDARD LVCMOS33} [get_ports esp_irq]
set_property -dict {PACKAGE_PIN M17 IOSTANDARD LVCMOS33} [get_ports prn_txd]
set_property -dict {PACKAGE_PIN D20 IOSTANDARD LVCMOS33} [get_ports prn_rxd]
set_property -dict {PACKAGE_PIN D19 IOSTANDARD LVCMOS33} [get_ports prn_cts]
set_property -dict {PACKAGE_PIN E19 IOSTANDARD LVCMOS33} [get_ports adxl_cs]
set_property -dict {PACKAGE_PIN E18 IOSTANDARD LVCMOS33} [get_ports adxl_drdy]
set_property -dict {PACKAGE_PIN G18 IOSTANDARD LVCMOS33} [get_ports imu_cs]
set_property -dict {PACKAGE_PIN G17 IOSTANDARD LVCMOS33} [get_ports imu_int1]
set_property -dict {PACKAGE_PIN H17 IOSTANDARD LVCMOS33} [get_ports imu_int2]
set_property -dict {PACKAGE_PIN H16 IOSTANDARD LVCMOS33} [get_ports mic_clk]
set_property -dict {PACKAGE_PIN G15 IOSTANDARD LVCMOS33} [get_ports mic_dat]
set_property -dict {PACKAGE_PIN H15 IOSTANDARD LVCMOS33} [get_ports spi_miso]
set_property -dict {PACKAGE_PIN J14 IOSTANDARD LVCMOS33} [get_ports spi_sclk]
set_property -dict {PACKAGE_PIN K14 IOSTANDARD LVCMOS33} [get_ports spi_mosi]

## 输出：慢边沿、8mA
set_property -dict {SLEW SLOW DRIVE 8} [get_ports {led1_pwm led2_pwm ws_data}]
set_property -dict {SLEW SLOW DRIVE 8} [get_ports {esp_txd esp_rts prn_txd}]
set_property -dict {SLEW SLOW DRIVE 8} [get_ports {adxl_cs imu_cs spi_sclk spi_mosi}]
set_property -dict {SLEW SLOW DRIVE 8} [get_ports mic_clk]

## I2C 在 HDL 中用 IOBUF 实现开漏（只拉低或高阻），外部已有 4.7k 上拉
## J706 接 MP34DT01（原 I2C2 网络）：mic_clk 为 pdm_mic 推挽 2MHz 时钟，R710 上拉仍在（低电平时多约 0.7mA）；
## mic_dat 在 pdm_mic 中两级同步，R711 上拉使模块缺失或采样相位不对时读到全 1
## 外部按键、旋钮有外部上拉，不需要内部上拉；编码器A/B及按压2.2k，K101为10k
## 异步输入（k1 enc_* *_int* tach_in pfail_n rtc_1hz esp_* prn_*）在 PL 中先两级同步
## 打印机电源 PRN_PWR_EN、发送使能 PRN_COM_EN 在 TCA9555（I2C0）上，不占 J11
