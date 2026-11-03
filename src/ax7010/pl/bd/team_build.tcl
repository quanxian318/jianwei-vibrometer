# Last stage of team_design.tcl: HDL wrapper outputs, implementation and the
# bitstream with its reports in <work>/out.
set bdf [get_files design_1.bd]
generate_target all $bdf

file mkdir $root/out
reset_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 1
wait_on_run impl_1
puts "IMPL status: [get_property STATUS [get_runs impl_1]]"
open_run impl_1
report_utilization -file $root/out/utilization.rpt
report_utilization -hierarchical -hierarchical_depth 2 -file $root/out/utilization_hier.rpt
report_timing_summary -max_paths 20 -file $root/out/timing.rpt
puts "WNS [get_property SLACK [get_timing_paths -setup]] WHS [get_property SLACK [get_timing_paths -hold]]"
file copy -force [get_property DIRECTORY [get_runs impl_1]]/design_1_wrapper.bit $root/out/design.bit
puts "BUILD DONE"
