set root C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/nano_uart
file mkdir $root/out
open_project C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/lite/project/lite.xpr
save_project_as nano_uart $root/project
set bdf [get_files design_1.bd]
if {![string match "$root/project/*" $bdf]} {error "Project isolation failed: $bdf"}
open_bd_design $bdf
# Temporarily route existing PS UART0 from printer connector to Nano UART1.
# Original prn_* port names are retained internally; printer is disabled in PS.
create_bd_port -dir O nano_rts_n
connect_bd_net [get_bd_pins processing_system7_0/UART0_RTSN] [get_bd_ports nano_rts_n]
set old [get_files -quiet *printer_pins.xdc]
if {[llength $old] != 1} {error "Expected one printer pin constraint file"}
remove_files $old
set f [open $root/nano_uart_pins.xdc w]
puts $f {set_property -dict {PACKAGE_PIN H20 IOSTANDARD LVCMOS33 SLEW SLOW DRIVE 8} [get_ports prn_txd]}
puts $f {set_property -dict {PACKAGE_PIN J20 IOSTANDARD LVCMOS33 PULLUP TRUE} [get_ports prn_rxd]}
puts $f {set_property -dict {PACKAGE_PIN L16 IOSTANDARD LVCMOS33 PULLUP TRUE} [get_ports prn_cts]}
puts $f {set_property -dict {PACKAGE_PIN L17 IOSTANDARD LVCMOS33 SLEW SLOW DRIVE 8} [get_ports nano_rts_n]}
close $f
add_files -fileset constrs_1 $root/nano_uart_pins.xdc
validate_bd_design
save_bd_design
generate_target all $bdf
make_wrapper -files $bdf -top
reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
open_run impl_1
report_utilization -file $root/out/utilization.rpt
report_timing_summary -file $root/out/timing.rpt
report_drc -file $root/out/drc.rpt
report_io -file $root/out/io.rpt
puts "NANO_UART_WNS [get_property SLACK [get_timing_paths -setup]] WHS [get_property SLACK [get_timing_paths -hold]]"
file copy -force [get_property DIRECTORY [get_runs impl_1]]/design_1_wrapper.bit $root/out/nano_uart.bit
close_project
