set root C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/lite
file mkdir $root/out
open_project C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/sht40/project/sht40.xpr
save_project_as lite $root/project
set bdf [get_files design_1.bd]
if {![string match "$root/project/*" $bdf]} {error "Project isolation failed: $bdf"}
open_bd_design $bdf
# Keep all acquisition/display peripherals. Remove dedicated disparity engine.
foreach p {M06 M07 M08 M09} {
 set n [get_bd_intf_nets -quiet -of_objects [get_bd_intf_pins processing_system7_0_axi_periph/${p}_AXI]]
 if {[llength $n]} {delete_bd_objs $n}
}
delete_bd_objs [get_bd_cells {stereo_0 stereo_dma bm_mem_intercon rst_stereo_40M}]
set_property CONFIG.PCW_USE_S_AXI_HP1 0 [get_bd_cells processing_system7_0]
set_property CONFIG.NUM_MI 8 [get_bd_cells processing_system7_0_axi_periph]
connect_bd_intf_net [get_bd_intf_pins processing_system7_0_axi_periph/M06_AXI] [get_bd_intf_pins pdm_mic_0/s_axi]
connect_bd_intf_net [get_bd_intf_pins processing_system7_0_axi_periph/M07_AXI] [get_bd_intf_pins lamp_gpio/S_AXI]
set clk [get_bd_nets -of_objects [get_bd_pins processing_system7_0/FCLK_CLK0]]
set rst [get_bd_nets -of_objects [get_bd_pins lamp_gpio/s_axi_aresetn]]
foreach port {M06 M07} {
 foreach suffix {ACLK ARESETN} {
  set pin [get_bd_pins processing_system7_0_axi_periph/${port}_${suffix}]
  set old [get_bd_nets -quiet -of_objects $pin]
  if {[llength $old]} {disconnect_bd_net $old $pin}
  if {$suffix eq "ACLK"} {connect_bd_net -net $clk $pin} else {connect_bd_net -net $rst $pin}
 }
}
assign_bd_address
foreach {seg base} {pdm_mic_0/s_axi/reg0 0x44030000 lamp_gpio/S_AXI/Reg 0x44070000} {
 set obj [get_bd_addr_segs -quiet $seg]
 if {![llength $obj]} {error "Missing address segment $seg"}
 assign_bd_address -offset $base -range 64K -target_address_space [get_bd_addr_spaces processing_system7_0/Data] $obj -force
}
validate_bd_design
save_bd_design
generate_target all $bdf
make_wrapper -files $bdf -top
reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
open_run impl_1
report_utilization -file $root/out/utilization.rpt
report_utilization -hierarchical -file $root/out/utilization_hier.rpt
report_timing_summary -file $root/out/timing.rpt
report_drc -file $root/out/drc.rpt
report_io -file $root/out/io.rpt
puts "LITE_WNS [get_property SLACK [get_timing_paths -setup]] WHS [get_property SLACK [get_timing_paths -hold]]"
file copy -force [get_property DIRECTORY [get_runs impl_1]]/design_1_wrapper.bit $root/out/lite.bit
close_project
