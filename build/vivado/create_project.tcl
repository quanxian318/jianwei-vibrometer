# Vivado 2025.2: vivado -mode batch -source create_project.tcl
# Add -tclargs build to run synthesis, implementation and bitstream generation.
set root [file normalize [file dirname [info script]]]
create_project current [file join $root build/project] -part xc7z010clg400-1 -force
set_property target_language Verilog [current_project]
set_property ip_repo_paths [list [file join $root ip_repo/repo0] [file join $root ip_repo/repo1]] [current_project]
update_ip_catalog
add_files [glob [file join $root rtl *.v]]
add_files -fileset constrs_1 [glob [file join $root constraints *.xdc]]
add_files [file join $root bd design_1.bd]
open_bd_design [file join $root bd design_1.bd]
validate_bd_design
generate_target all [get_files design_1.bd]
set_property top design_1_wrapper [current_fileset]
update_compile_order -fileset sources_1
if {[llength $argv] && [lindex $argv 0] eq "build"} {
  launch_runs synth_1 -jobs 2
  wait_on_run synth_1
  if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "Synthesis incomplete"}
  launch_runs impl_1 -to_step write_bitstream -jobs 2
  wait_on_run impl_1
  if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Implementation incomplete"}
}
close_project
