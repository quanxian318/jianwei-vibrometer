set root C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/panel
if {[file exists $root/out/panel.bit]} {file copy -force $root/out/panel.bit $root/out/panel_previous.bit}
if {[file exists $root/out/panel.bit.bin]} {file copy -force $root/out/panel.bit.bin $root/out/panel_previous.bit.bin}
open_project $root/project/panel.xpr
open_bd_design [get_files design_1.bd]
update_compile_order -fileset sources_1
set panelIp [get_ips -quiet *panel_probe*]
if {[llength $panelIp] != 1} {error "Expected one panel module IP, found: $panelIp"}
update_module_reference -verbose $panelIp
validate_bd_design
save_bd_design
generate_target all [get_files design_1.bd]
make_wrapper -files [get_files design_1.bd] -top
reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
open_run impl_1
report_timing_summary -file $root/out/timing_static.rpt
report_drc -file $root/out/drc_static.rpt
report_utilization -file $root/out/utilization_static.rpt
report_io -file $root/out/io_static.rpt
puts "PANEL_STATIC_WNS [get_property SLACK [get_timing_paths -setup]] WHS [get_property SLACK [get_timing_paths -hold]]"
file copy -force [get_property DIRECTORY [get_runs impl_1]]/design_1_wrapper.bit $root/out/panel_static.bit
close_project
