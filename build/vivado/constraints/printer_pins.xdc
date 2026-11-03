set_property -dict {PACKAGE_PIN M17 IOSTANDARD LVCMOS33 SLEW SLOW DRIVE 8} [get_ports printer_txd]
set_property -dict {PACKAGE_PIN D20 IOSTANDARD LVCMOS33 PULLUP TRUE} [get_ports printer_rxd]
set_property -dict {PACKAGE_PIN D19 IOSTANDARD LVCMOS33 PULLUP TRUE} [get_ports printer_ctsn]
