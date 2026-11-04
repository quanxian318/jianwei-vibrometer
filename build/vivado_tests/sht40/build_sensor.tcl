set root C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/sht40
open_project $root/project/sht40.xpr
set bdf [get_files design_1.bd]
if {![string match "$root/project/*" $bdf]} {error "BD outside isolated copy: $bdf"}
open_bd_design $bdf
update_compile_order -fileset sources_1
set def [get_property VLNV [get_bd_cells carrier_iic_branch]]
create_bd_cell -type ip -vlnv $def sensor_iic_branch
disconnect_bd_intf_net [get_bd_intf_nets -of_objects [get_bd_intf_ports carrier_mgmt_i2c]] [get_bd_intf_ports carrier_mgmt_i2c]
connect_bd_intf_net [get_bd_intf_pins carrier_iic_branch/i2c_down1] [get_bd_intf_pins sensor_iic_branch/i2c_up]
connect_bd_intf_net [get_bd_intf_pins sensor_iic_branch/i2c_down0] [get_bd_intf_ports carrier_mgmt_i2c]
create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 carrier_sens_i2c
connect_bd_intf_net [get_bd_intf_pins sensor_iic_branch/i2c_down1] [get_bd_intf_ports carrier_sens_i2c]
validate_bd_design
save_bd_design
set f [open $root/sensor_pins.xdc w]
puts $f {set_property -dict {PACKAGE_PIN M20 IOSTANDARD LVCMOS33 PULLUP TRUE} [get_ports carrier_sens_i2c_scl_io]}
puts $f {set_property -dict {PACKAGE_PIN M19 IOSTANDARD LVCMOS33 PULLUP TRUE} [get_ports carrier_sens_i2c_sda_io]}
close $f
add_files -fileset constrs_1 $root/sensor_pins.xdc
generate_target all $bdf
make_wrapper -files $bdf -top
file mkdir $root/out
reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Implementation incomplete"}
open_run impl_1
report_utilization -file $root/out/utilization.rpt
report_timing_summary -max_paths 20 -file $root/out/timing.rpt
report_drc -file $root/out/drc.rpt
foreach p {carrier_sens_i2c_scl_io carrier_sens_i2c_sda_io} {
 puts "SENSOR_PIN $p [get_property PACKAGE_PIN [get_ports $p]]"
}
puts "SENSOR_WNS [get_property SLACK [get_timing_paths -setup]] SENSOR_WHS [get_property SLACK [get_timing_paths -hold]]"
file copy -force [get_property DIRECTORY [get_runs impl_1]]/design_1_wrapper.bit $root/out/sensor.bit
close_project
