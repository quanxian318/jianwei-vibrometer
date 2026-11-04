set root C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/printer_uart
file mkdir $root/out
open_project C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/nano_uart/project/nano_uart.xpr
save_project_as printer_uart $root/project
set bdf [get_files design_1.bd]
if {![string match "$root/project/*" $bdf]} {error "Project isolation failed: $bdf"}
open_bd_design $bdf
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_uart16550:2.0 printer_uart
set_property -dict [list CONFIG.C_HAS_EXTERNAL_XIN {0} CONFIG.C_HAS_EXTERNAL_RCLK {0}] [get_bd_cells printer_uart]
# Keep the Nano on PS UART0 and add a separate UART on the original J401 pins.
create_bd_port -dir O printer_txd
create_bd_port -dir I printer_rxd
create_bd_port -dir I printer_ctsn
connect_bd_net [get_bd_pins printer_uart/sout] [get_bd_ports printer_txd]
connect_bd_net [get_bd_pins printer_uart/sin] [get_bd_ports printer_rxd]
connect_bd_net [get_bd_pins printer_uart/ctsn] [get_bd_ports printer_ctsn]
foreach p {dcdn dsrn rin} {
  create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 printer_${p}_high
  set_property CONFIG.CONST_VAL 1 [get_bd_cells printer_${p}_high]
  connect_bd_net [get_bd_pins printer_${p}_high/dout] [get_bd_pins printer_uart/$p]
}
apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Master "/processing_system7_0/M_AXI_GP0" Slave "/printer_uart/S_AXI" Clk "/processing_system7_0/FCLK_CLK0 (100 MHz)"} [get_bd_intf_pins printer_uart/S_AXI]
assign_bd_address -offset 0x44050000 -range 0x00010000 -target_address_space [get_bd_addr_spaces processing_system7_0/Data] [get_bd_addr_segs printer_uart/S_AXI/Reg] -force
set f [open $root/printer_pins.xdc w]
puts $f {set_property -dict {PACKAGE_PIN M17 IOSTANDARD LVCMOS33 SLEW SLOW DRIVE 8} [get_ports printer_txd]}
puts $f {set_property -dict {PACKAGE_PIN D20 IOSTANDARD LVCMOS33 PULLUP TRUE} [get_ports printer_rxd]}
puts $f {set_property -dict {PACKAGE_PIN D19 IOSTANDARD LVCMOS33 PULLUP TRUE} [get_ports printer_ctsn]}
close $f
add_files -fileset constrs_1 $root/printer_pins.xdc
validate_bd_design
save_bd_design
generate_target all $bdf
make_wrapper -files $bdf -top
reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Implementation did not finish"}
open_run impl_1
report_utilization -file $root/out/utilization.rpt
report_timing_summary -file $root/out/timing.rpt
report_drc -file $root/out/drc.rpt
report_io -file $root/out/io.rpt
puts "PRINTER_UART_WNS [get_property SLACK [get_timing_paths -setup]] WHS [get_property SLACK [get_timing_paths -hold]]"
file copy -force [get_property DIRECTORY [get_runs impl_1]]/design_1_wrapper.bit $root/out/printer_uart.bit
close_project
