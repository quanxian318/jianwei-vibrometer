# The team's AN5642 camera/HDMI design (11_队友Vivado工程_20261021,
# AX7010_Sdoira_PL_V1) with its idle sdoira BM replaced by the vibmon
# census+SGM stereo core (stereo_axi) and an AXI DMA on the BM's HP1 path.
# One stage of team_design.tcl (block design changes only, no build).
#
# Addresses: stereo_0 0x43C2_0000 (the BM's slot), stereo_dma 0x4040_0000.
# The SGM sweep has a ~19.4 ns combinational path (cost read -> path minimum
# -> right-disparity buffer), so the core and its DMA run on FCLK2 = 40 MHz
# (about 61 ms per 256 x 144 pair). FCLK1/FCLK2 frequencies come from the
# FSBL at boot; with the ALINX boot files prepare_an5642_runtime.py sets them
# before loading the bitstream.
# The DMA's memory side is 64 bits wide and S_AXI_HP1 runs on the same 40 MHz
# clock, so the interconnect has no clock or width converters (the design is
# short of slices; the stream needs about 1.2 MB/s each way).
# BM matcher, its constant DVP drivers and its sources.
delete_bd_objs [get_bd_cells {sdoira_bm_axi_0 bm_zero bm_zero_bit}]
set bmsrc [get_files -quiet -of_objects [get_filesets sources_1] -filter {NAME =~ "*/vendor/rtl/*.v" || NAME =~ "*/rtl/sdoira_*.v" || NAME =~ "*/vendor/ip/*.xci"}]
if {[llength $bmsrc]} { remove_files $bmsrc }

add_files -norecurse [glob $root/stereo_rtl/*.v]
update_compile_order -fileset sources_1

proc latest {name} { return [lindex [lsort [get_ipdefs -filter "VLNV =~ xilinx.com:ip:${name}:*"]] end] }

create_bd_cell -type module -reference stereo_axi stereo_0
set_property CONFIG.MAXW 320 [get_bd_cells stereo_0]
create_bd_cell -type ip -vlnv [latest axi_dma] stereo_dma
set_property -dict [list CONFIG.c_include_sg {0} CONFIG.c_sg_length_width {23} \
    CONFIG.c_include_mm2s {1} CONFIG.c_include_s2mm {1} \
    CONFIG.c_m_axis_mm2s_tdata_width {16} CONFIG.c_s_axis_s2mm_tdata_width {16} \
    CONFIG.c_m_axi_mm2s_data_width {64} CONFIG.c_m_axi_s2mm_data_width {64} \
    CONFIG.c_mm2s_burst_size {16} CONFIG.c_s2mm_burst_size {16}] [get_bd_cells stereo_dma]

# 40 MHz domain for the stereo core and its DMA.
set ps [get_bd_cells processing_system7_0]
set_property -dict [list CONFIG.PCW_EN_CLK2_PORT {1} CONFIG.PCW_FPGA2_PERIPHERAL_FREQMHZ {40}] $ps
create_bd_cell -type ip -vlnv [latest proc_sys_reset] rst_stereo_40M
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK2] [get_bd_pins rst_stereo_40M/slowest_sync_clk]
connect_bd_net -net [get_bd_nets -of_objects [get_bd_pins processing_system7_0/FCLK_RESET0_N]] \
    [get_bd_pins rst_stereo_40M/ext_reset_in]
set clk [get_bd_nets -of_objects [get_bd_pins processing_system7_0/FCLK_CLK2]]
set rstn [get_bd_pins rst_stereo_40M/peripheral_aresetn]

# Registers: the BM's M06 slot for the core, a new M07 for the DMA.
set_property CONFIG.NUM_MI 8 [get_bd_cells processing_system7_0_axi_periph]
connect_bd_intf_net [get_bd_intf_pins processing_system7_0_axi_periph/M06_AXI] [get_bd_intf_pins stereo_0/s_axi]
connect_bd_intf_net [get_bd_intf_pins processing_system7_0_axi_periph/M07_AXI] [get_bd_intf_pins stereo_dma/S_AXI_LITE]
# Memory: both DMA channels through the BM's interconnect to S_AXI_HP1.
set_property CONFIG.NUM_SI 2 [get_bd_cells bm_mem_intercon]
connect_bd_intf_net [get_bd_intf_pins stereo_dma/M_AXI_MM2S] [get_bd_intf_pins bm_mem_intercon/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins stereo_dma/M_AXI_S2MM] [get_bd_intf_pins bm_mem_intercon/S01_AXI]
connect_bd_intf_net [get_bd_intf_pins stereo_dma/M_AXIS_MM2S] [get_bd_intf_pins stereo_0/s_axis]
connect_bd_intf_net [get_bd_intf_pins stereo_0/m_axis] [get_bd_intf_pins stereo_dma/S_AXIS_S2MM]

# The BM's ports were on other clocks: move them, then clock everything stereo,
# the whole memory interconnect and S_AXI_HP1 at 40 MHz.
set moved {processing_system7_0_axi_periph/M06_ACLK bm_mem_intercon/ACLK bm_mem_intercon/S00_ACLK
           bm_mem_intercon/M00_ACLK processing_system7_0/S_AXI_HP1_ACLK
           processing_system7_0_axi_periph/M06_ARESETN bm_mem_intercon/ARESETN bm_mem_intercon/S00_ARESETN
           bm_mem_intercon/M00_ARESETN}
foreach p $moved {
    set n [get_bd_nets -quiet -of_objects [get_bd_pins $p]]
    if {$n ne ""} { disconnect_bd_net $n [get_bd_pins $p] }
}
foreach p {stereo_0/s_axi_aclk stereo_dma/s_axi_lite_aclk stereo_dma/m_axi_mm2s_aclk stereo_dma/m_axi_s2mm_aclk
           processing_system7_0_axi_periph/M06_ACLK processing_system7_0_axi_periph/M07_ACLK
           bm_mem_intercon/ACLK bm_mem_intercon/S00_ACLK bm_mem_intercon/S01_ACLK bm_mem_intercon/M00_ACLK
           processing_system7_0/S_AXI_HP1_ACLK} {
    connect_bd_net -net $clk [get_bd_pins $p]
}
connect_bd_net [get_bd_pins rst_stereo_40M/interconnect_aresetn] [get_bd_pins bm_mem_intercon/ARESETN]
foreach p {stereo_0/s_axi_aresetn stereo_dma/axi_resetn
           processing_system7_0_axi_periph/M06_ARESETN processing_system7_0_axi_periph/M07_ARESETN
           bm_mem_intercon/S00_ARESETN bm_mem_intercon/S01_ARESETN bm_mem_intercon/M00_ARESETN} {
    connect_bd_net $rstn [get_bd_pins $p]
}

assign_bd_address
set space [get_bd_addr_spaces processing_system7_0/Data]
foreach {pat base range} {*stereo_0* 0x43C20000 4K *stereo_dma* 0x40400000 64K} {
    set seg [get_bd_addr_segs -of_objects $space -filter "NAME =~ $pat"]
    set_property range $range $seg
    set_property offset $base $seg
}
foreach seg [get_bd_addr_segs -of_objects $space] { puts "ADDR [get_property NAME $seg] [get_property OFFSET $seg] [get_property RANGE $seg]" }
validate_bd_design
save_bd_design
