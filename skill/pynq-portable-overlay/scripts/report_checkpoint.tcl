# Vivado batch entry point for a routed checkpoint.
if {$argc != 2} {
    error "Usage: -tclargs <routed.dcp> <output_directory>"
}
set report_script [file join [file dirname [file normalize [info script]]] vivado_reports.tcl]
set checkpoint [file normalize [lindex $argv 0]]
set destination [file normalize [lindex $argv 1]]
if {![file isfile $checkpoint]} { error "Missing checkpoint: $checkpoint" }
open_checkpoint $checkpoint
file mkdir $destination
cd $destination
source $report_script

