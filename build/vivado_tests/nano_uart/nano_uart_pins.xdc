set_property -dict {PACKAGE_PIN H20 IOSTANDARD LVCMOS33 SLEW SLOW DRIVE 8} [get_ports prn_txd]
set_property -dict {PACKAGE_PIN J20 IOSTANDARD LVCMOS33 PULLUP TRUE} [get_ports prn_rxd]
set_property -dict {PACKAGE_PIN L16 IOSTANDARD LVCMOS33 PULLUP TRUE} [get_ports prn_cts]
set_property -dict {PACKAGE_PIN L17 IOSTANDARD LVCMOS33 SLEW SLOW DRIVE 8} [get_ports nano_rts_n]
