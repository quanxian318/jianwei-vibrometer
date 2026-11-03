# MP34DT01 PDM microphone receiver (pdm_mic, ../audio/rtl) in the team's
# AN5642 design: J11-32 = H16 = mic_clk (output), J11-33 = G15 = mic_dat
# (input), the pins of J706 through J801 on the carrier board (or a module
# wired straight to J11). One stage of team_design.tcl (block design changes
# only). AXI4-Lite at 0x4403_0000 on FCLK0 (100 MHz -> 2 MHz PDM clock); no
# interrupt: the PS polls the 4096-sample FIFO (131 ms at 31.25 kS/s).
set pdm_addr 0x44030000

add_files -norecurse [glob $root/audio_rtl/*.v]
add_files -fileset constrs_1 -norecurse $root/pdm_mic_team.xdc
update_compile_order -fileset sources_1

create_bd_cell -type module -reference pdm_mic pdm_mic_0
set_property CONFIG.CLK_HZ 100000000 [get_bd_cells pdm_mic_0]
set periph [get_bd_cells processing_system7_0_axi_periph]
set mi [get_property CONFIG.NUM_MI $periph]
set port [format M%02d $mi]
set_property CONFIG.NUM_MI [expr {$mi + 1}] $periph
connect_bd_intf_net [get_bd_intf_pins $periph/${port}_AXI] [get_bd_intf_pins pdm_mic_0/s_axi]
set clk0 [get_bd_nets -of_objects [get_bd_pins processing_system7_0/FCLK_CLK0]]
set rst0 [get_bd_nets -of_objects [get_bd_pins rst_processing_system7_0_100M/peripheral_aresetn]]
foreach p [list pdm_mic_0/s_axi_aclk $periph/${port}_ACLK] { connect_bd_net -net $clk0 [get_bd_pins $p] }
foreach p [list pdm_mic_0/s_axi_aresetn $periph/${port}_ARESETN] { connect_bd_net -net $rst0 [get_bd_pins $p] }

# No power-fail input in this design: the clock only stops when the PS disables it.
create_bd_cell -type ip -vlnv [lindex [lsort [get_ipdefs -filter {VLNV =~ xilinx.com:ip:xlconstant:*}]] end] pdm_kill_tie
set_property -dict [list CONFIG.CONST_WIDTH {1} CONFIG.CONST_VAL {0}] [get_bd_cells pdm_kill_tie]
connect_bd_net [get_bd_pins pdm_kill_tie/dout] [get_bd_pins pdm_mic_0/kill]

create_bd_port -dir O mic_clk
create_bd_port -dir I mic_dat
connect_bd_net [get_bd_ports mic_clk] [get_bd_pins pdm_mic_0/mic_clk]
connect_bd_net [get_bd_ports mic_dat] [get_bd_pins pdm_mic_0/mic_dat]

assign_bd_address
set space [get_bd_addr_spaces processing_system7_0/Data]
set seg [get_bd_addr_segs -of_objects $space -filter {NAME =~ *pdm_mic_0*}]
set_property range 4K $seg
set_property offset $pdm_addr $seg
foreach seg [get_bd_addr_segs -of_objects $space] { puts "ADDR [get_property NAME $seg] [get_property OFFSET $seg] [get_property RANGE $seg]" }
validate_bd_design
save_bd_design
