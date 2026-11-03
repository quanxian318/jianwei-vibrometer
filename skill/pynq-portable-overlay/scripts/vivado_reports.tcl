# Source in Vivado with an implemented design open.
if {[catch {current_design} design] || $design eq ""} {
    error "Open the implemented design before sourcing this script"
}
file mkdir reports
report_utilization -file reports/utilization.rpt
report_timing_summary -report_unconstrained -file reports/timing_summary.rpt
report_drc -file reports/drc.rpt
set f [open reports/build_context.txt w]
puts $f "Vivado: [version -short]"
puts $f "Design: $design"
foreach c [get_clocks] { puts $f "$c period_ns=[get_property PERIOD $c]" }
close $f

