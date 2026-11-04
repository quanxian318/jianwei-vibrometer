set root C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/panel
open_project C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/sht40/project/sht40.xpr
save_project_as panel $root/project
if {![string match "$root/*" [get_property DIRECTORY [current_project]]]} {error "Project isolation failed"}
open_bd_design [get_files design_1.bd]
set src D:/AX7010_Multipoint_Acceleration/ax7010/pl/rtl
foreach name {cc_quad.v cc_ws2812.v} {
 file copy -force $src/$name $root/$name
 add_files -norecurse $root/$name
}
add_files -norecurse $root/panel_probe.v
update_compile_order -fileset sources_1
create_bd_cell -type module -reference panel_probe panel_probe_0
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio:2.0 panel_gpio
set_property -dict [list CONFIG.C_GPIO_WIDTH 32 CONFIG.C_ALL_OUTPUTS 1 CONFIG.C_IS_DUAL 1 CONFIG.C_GPIO2_WIDTH 32 CONFIG.C_ALL_INPUTS_2 1] [get_bd_cells panel_gpio]
set periph [get_bd_cells processing_system7_0_axi_periph]
set mi [get_property CONFIG.NUM_MI $periph]
set port [format M%02d $mi]
set_property CONFIG.NUM_MI [expr {$mi+1}] $periph
connect_bd_intf_net [get_bd_intf_pins $periph/${port}_AXI] [get_bd_intf_pins panel_gpio/S_AXI]
set clk0 [get_bd_nets -of_objects [get_bd_pins processing_system7_0/FCLK_CLK0]]
set mhz [get_property CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ [get_bd_cells processing_system7_0]]
if {abs($mhz-100)>0.1} {error "Expected FCLK0 100MHz, got $mhz"}
set rst0 [get_bd_nets -of_objects [get_bd_pins rst_processing_system7_0_100M/peripheral_aresetn]]
foreach p [list panel_gpio/s_axi_aclk panel_probe_0/clk $periph/${port}_ACLK] {connect_bd_net -net $clk0 [get_bd_pins $p]}
foreach p [list panel_gpio/s_axi_aresetn panel_probe_0/resetn $periph/${port}_ARESETN] {connect_bd_net -net $rst0 [get_bd_pins $p]}
connect_bd_net [get_bd_pins panel_gpio/gpio_io_o] [get_bd_pins panel_probe_0/command]
connect_bd_net [get_bd_pins panel_gpio/gpio2_io_i] [get_bd_pins panel_probe_0/status]
foreach p {k1 enc_sw enc_a enc_b} {
 create_bd_port -dir I $p
 connect_bd_net [get_bd_ports $p] [get_bd_pins panel_probe_0/$p]
}
create_bd_port -dir O ws_data
connect_bd_net [get_bd_ports ws_data] [get_bd_pins panel_probe_0/ws_data]
assign_bd_address -offset 0x44080000 -range 64K -target_address_space [get_bd_addr_spaces processing_system7_0/Data] [get_bd_addr_segs panel_gpio/S_AXI/Reg] -force
validate_bd_design
save_bd_design
set f [open $root/panel_pins.xdc w]
foreach {p pin} {k1 F20 enc_sw K17 enc_a J19 enc_b K19 ws_data F19} {
 puts $f "set_property -dict {PACKAGE_PIN $pin IOSTANDARD LVCMOS33} \[get_ports $p\]"
}
puts $f {set_property -dict {SLEW SLOW DRIVE 8} [get_ports ws_data]}
puts $f {set_false_path -from [get_ports {k1 enc_sw enc_a enc_b}] -to [get_cells -hier -filter {NAME =~ *panel_probe*sync1_reg*}]}
close $f
add_files -fileset constrs_1 $root/panel_pins.xdc
generate_target all [get_files design_1.bd]
make_wrapper -files [get_files design_1.bd] -top
file mkdir $root/out
reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Implementation incomplete"}
open_run impl_1
report_timing_summary -file $root/out/timing.rpt
report_drc -file $root/out/drc.rpt
report_utilization -file $root/out/utilization.rpt
report_io -file $root/out/io.rpt
puts "PANEL_WNS [get_property SLACK [get_timing_paths -setup]] PANEL_WHS [get_property SLACK [get_timing_paths -hold]]"
file copy -force [get_property DIRECTORY [get_runs impl_1]]/design_1_wrapper.bit $root/out/panel.bit
close_project
