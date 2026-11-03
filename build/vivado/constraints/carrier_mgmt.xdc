set_property -dict {PACKAGE_PIN L20 IOSTANDARD LVCMOS33 PULLUP TRUE} [get_ports carrier_mgmt_i2c_scl_io]
set_property -dict {PACKAGE_PIN L19 IOSTANDARD LVCMOS33 PULLUP TRUE} [get_ports carrier_mgmt_i2c_sda_io]
set_property PULLUP TRUE [get_ports {cmos1_i2c_scl_io cmos1_i2c_sda_io}]
