set root C:/Users/HP/Documents/Codex/2026-10-28/fpga/work/panel
create_project panel_sim $root/sim -part xc7z010clg400-1 -force
foreach f {panel_probe.v tb_panel_probe.v} {add_files -fileset sim_1 $root/$f}
foreach f {cc_quad.v cc_ws2812.v} {add_files -fileset sim_1 D:/AX7010_Multipoint_Acceleration/ax7010/pl/rtl/$f}
set_property top tb_panel_probe [get_filesets sim_1]
set_property xsim.simulate.runtime 3ms [get_filesets sim_1]
launch_simulation
close_sim
close_project
