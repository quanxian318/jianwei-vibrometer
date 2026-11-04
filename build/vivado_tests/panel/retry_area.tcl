set root C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/panel
open_project $root/project/panel.xpr
set_property strategy Area_Explore [get_runs impl_1]
reset_run impl_1
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
