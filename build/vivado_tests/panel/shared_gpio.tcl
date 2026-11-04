set root C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/panel
open_project $root/project/panel.xpr
open_bd_design [get_files design_1.bd]
set p [get_bd_cells processing_system7_0_axi_periph]
set n [get_property CONFIG.NUM_MI $p]
delete_bd_objs [get_bd_cells panel_gpio]
set_property CONFIG.NUM_MI [expr {$n-1}] $p
# The two consumers use the old output net, so detach the lamp command only.
set lampCmd [get_bd_pins lamp_pwm_0/command]
disconnect_bd_net [get_bd_nets -of_objects $lampCmd] $lampCmd
set panelCmd [get_bd_pins panel_probe_0/command]
set old [get_bd_nets -quiet -of_objects $panelCmd]
if {$old ne ""} {disconnect_bd_net $old $panelCmd}
connect_bd_net [get_bd_pins lamp_gpio/gpio_io_o] $panelCmd
create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 panel_lamp_off
set_property -dict [list CONFIG.CONST_WIDTH 32 CONFIG.CONST_VAL 0] [get_bd_cells panel_lamp_off]
connect_bd_net [get_bd_pins panel_lamp_off/dout] $lampCmd
set gpioStatus [get_bd_pins lamp_gpio/gpio2_io_i]
disconnect_bd_net [get_bd_nets -of_objects $gpioStatus] $gpioStatus
set panelStatus [get_bd_pins panel_probe_0/status]
set old [get_bd_nets -quiet -of_objects $panelStatus]
if {$old ne ""} {disconnect_bd_net $old $panelStatus}
connect_bd_net $panelStatus $gpioStatus
validate_bd_design
save_bd_design
generate_target all [get_files design_1.bd]
make_wrapper -files [get_files design_1.bd] -top
set_property strategy Area_Explore [get_runs impl_1]
reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
open_run impl_1
report_timing_summary -file $root/out/timing.rpt
report_drc -file $root/out/drc.rpt
report_utilization -file $root/out/utilization.rpt
report_io -file $root/out/io.rpt
puts "PANEL_WNS [get_property SLACK [get_timing_paths -setup]] PANEL_WHS [get_property SLACK [get_timing_paths -hold]]"
file copy -force [get_property DIRECTORY [get_runs impl_1]]/design_1_wrapper.bit $root/out/panel.bit
close_project
