# Adds the PL stereo matcher to the block design after add_carrier.tcl:
#
#   source <...>/src/ax7010/pl/bd/add_carrier.tcl
#   carrier::add
#   source <...>/src/ax7010/pl/bd/add_stereo.tcl
#   stereo::add                  ;# optional: stereo::add 320  (MAXW)
#
# Copy ax7010/pl to an ASCII-only folder first (Vivado and Chinese paths).
#
# Result:
#   stereo_0   module reference stereo_axi (../stereo/rtl): AXI4-Lite
#              registers at 0x4407_0000, AXI4-Stream in/out, DONE interrupt
#   stereo_dma AXI DMA, simple mode, 16-bit MM2S/S2MM streams, 23-bit length,
#              registers at 0x4408_0000, memory side on PS S_AXI_HP2
#   generated/stereo_generated.dtsi  reserved DDR window + UIO node
#   generated/stereo_map.json
# The team's sdoira BM (11_队友Vivado工程) is not used by this path; remove it
# from the integrated design to free its 17 BRAM36 (its original package stays intact).
#
# The DMA buffer is a reserved DDR window (default 1 MiB at 0x1F000000 on the
# 512 MiB AX7010): input pairs at +0, output disparities at +0x80000.

if {[info commands carrier::latest_vlnv] eq ""} {
    source [file join [file dirname [file normalize [info script]]] add_carrier.tcl]
}

namespace eval stereo {
    variable dir [file dirname [file normalize [info script]]]
    variable core_base 0x44070000
    variable dma_base  0x44080000
    variable buf_base  0x1F000000
    variable buf_size  0x00100000
}

proc stereo::add {{maxw 320}} {
    variable dir
    variable core_base
    variable dma_base

    set bd [current_bd_design -quiet]
    if {$bd eq ""} { error "open the block design first (open_bd_design ...)" }
    set ps_cell [get_bd_cells -filter {VLNV =~ "xilinx.com:ip:processing_system7:*"}]
    if {[llength $ps_cell] != 1} { error "expected exactly one processing_system7 cell" }
    set ps [get_property NAME $ps_cell]
    if {[get_bd_cells -quiet stereo_0] ne ""} { error "stereo_0 already exists" }
    if {[get_bd_cells -quiet carrier_ctrl_0] eq ""} { puts "stereo: WARNING carrier_ctrl_0 not found; run carrier::add first for the full system" }

    set rtl [file normalize "$dir/../stereo/rtl"]
    add_files -norecurse [glob $rtl/*.v]
    update_compile_order -fileset sources_1

    # Cells.
    create_bd_cell -type module -reference stereo_axi stereo_0
    set_property CONFIG.MAXW $maxw [get_bd_cells stereo_0]
    set dma [create_bd_cell -type ip -vlnv [carrier::latest_vlnv axi_dma] stereo_dma]
    set_property -dict [list \
        CONFIG.c_include_sg {0} \
        CONFIG.c_sg_length_width {23} \
        CONFIG.c_include_mm2s {1} CONFIG.c_include_s2mm {1} \
        CONFIG.c_m_axis_mm2s_tdata_width {16} CONFIG.c_s_axis_s2mm_tdata_width {16} \
        CONFIG.c_m_axi_mm2s_data_width {32} CONFIG.c_m_axi_s2mm_data_width {32} \
        CONFIG.c_mm2s_burst_size {64} CONFIG.c_s2mm_burst_size {64}] $dma

    # Register interfaces on M_AXI_GP0 (same automation as the carrier IPs).
    carrier::connect_axi $ps stereo_0 s_axi
    carrier::connect_axi $ps stereo_dma S_AXI_LITE

    # Streams.
    connect_bd_intf_net [get_bd_intf_pins stereo_dma/M_AXIS_MM2S] [get_bd_intf_pins stereo_0/s_axis]
    connect_bd_intf_net [get_bd_intf_pins stereo_0/m_axis] [get_bd_intf_pins stereo_dma/S_AXIS_S2MM]

    # Memory side on HP2 (HP0/HP1 are typically used by the camera/HDMI VDMAs).
    set_property CONFIG.PCW_USE_S_AXI_HP2 {1} $ps_cell
    foreach m {M_AXI_MM2S M_AXI_S2MM} {
        if {[catch {apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
                -config [list Clk_master {Auto} Clk_slave {Auto} Clk_xbar {Auto} Master "/stereo_dma/$m" Slave "/$ps/S_AXI_HP2" intc_ip {Auto} master_apm {0}] \
                [get_bd_intf_pins $ps/S_AXI_HP2]} err]} {
            puts "stereo: HP2 automation for $m (new keys) failed: $err; retrying with legacy keys"
            apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config [list Master "/stereo_dma/$m" Clk {Auto}] \
                [get_bd_intf_pins $ps/S_AXI_HP2]
        }
    }
    # One clock domain: the DMA stream/memory clocks follow the register clock of stereo_0.
    set clk [get_bd_nets -of_objects [get_bd_pins stereo_0/s_axi_aclk]]
    foreach p {m_axi_mm2s_aclk m_axi_s2mm_aclk s_axi_lite_aclk} {
        set pin [get_bd_pins -quiet stereo_dma/$p]
        if {$pin ne "" && [llength [get_bd_nets -quiet -of_objects $pin]] == 0} { connect_bd_net $clk $pin }
    }

    # Interrupt (DONE). The DMA completion interrupts are not needed: the
    # driver waits for the core's DONE and then checks the S2MM status.
    set irq_map [carrier::append_irqs $ps {stereo_0 irq}]

    # Fixed addresses.
    set space [get_bd_addr_spaces $ps/Data]
    set addr_out {}
    foreach {name base range} [list stereo_0 $core_base 4K stereo_dma $dma_base 64K] {
        set seg [get_bd_addr_segs -quiet -of_objects $space -filter "NAME =~ *${name}*"]
        if {[llength $seg] != 1} {
            puts "stereo: WARNING no unique address segment for $name (found: $seg)"
            continue
        }
        if {[catch {set_property range $range $seg; set_property offset $base $seg} err]} {
            puts "stereo: WARNING could not place $name at $base: $err"
        }
        lappend addr_out $name [format 0x%08X [get_property OFFSET $seg]]
    }
    # The DMA masters must reach the reserved DDR window through HP2.
    foreach m {Data_MM2S Data_S2MM} {
        set dsp [get_bd_addr_spaces -quiet stereo_dma/$m]
        if {$dsp ne "" && [llength [get_bd_addr_segs -quiet -of_objects $dsp]] == 0} { assign_bd_address -target_address_space $dsp }
    }

    validate_bd_design
    save_bd_design
    stereo::write_outputs $maxw $addr_out $irq_map
    puts "stereo: done. Regenerate the HDL wrapper; then synthesize and check timing/BRAM (see 14_双目深度改进 README)."
}

proc stereo::write_outputs {maxw addr_out irq_map} {
    variable dir
    variable buf_base
    variable buf_size
    set gen [file normalize "$dir/generated"]
    file mkdir $gen
    array set addr $addr_out
    array set irq $irq_map
    set core null
    set dma null
    set q null
    if {[info exists addr(stereo_0)]} { set core $addr(stereo_0) }
    if {[info exists addr(stereo_dma)]} { set dma $addr(stereo_dma) }
    if {[info exists irq(stereo_0)]} { set q $irq(stereo_0) }

    set f [open "$gen/stereo_map.json" w]
    puts $f "\{"
    puts $f "  \"maxw\": $maxw,"
    puts $f "  \"stereo_0\": \{\"axi_base\": \"$core\", \"irq_f2p\": $q\},"
    puts $f "  \"stereo_dma\": \{\"axi_base\": \"$dma\"\},"
    puts $f "  \"buffer\": \{\"base\": \"[format 0x%08X $buf_base]\", \"size\": \"[format 0x%08X $buf_size]\"\}"
    puts $f "\}"
    close $f

    set f [open "$gen/stereo_generated.dtsi" w]
    puts $f "/* Generated by add_stereo.tcl - include from system-user.dtsi */"
    puts $f "/ \{"
    puts $f "\treserved-memory \{"
    puts $f "\t\t#address-cells = <1>;"
    puts $f "\t\t#size-cells = <1>;"
    puts $f "\t\tranges;"
    puts $f "\t\tstereo_buf: stereo-buffer@[format %x $buf_base] \{"
    puts $f "\t\t\treg = <[format 0x%08x $buf_base] [format 0x%x $buf_size]>;"
    puts $f "\t\t\tno-map;"
    puts $f "\t\t\};"
    puts $f "\t\};"
    puts $f "\};"
    if {$core ne "null" && $dma ne "null" && $q ne "null"} {
        puts $f "&amba_pl \{"
        puts $f "\tstereo_uio: stereo@[string tolower [string range $core 2 end]] \{"
        puts $f "\t\t/* map0 = stereo_axi registers, map1 = AXI DMA registers, map2 = DMA buffer (uncached) */"
        puts $f "\t\tcompatible = \"vibmon,stereo\", \"vibmon,uio\";"
        puts $f "\t\treg = <$core 0x1000>, <$dma 0x10000>, <[format 0x%08x $buf_base] [format 0x%x $buf_size]>;"
        puts $f "\t\tinterrupt-parent = <&intc>;"
        puts $f "\t\tinterrupts = <0 [carrier::dt_irq $q] 4>;"
        puts $f "\t\};"
        puts $f "\};"
    }
    close $f
    puts "stereo: wrote $gen/stereo_map.json and stereo_generated.dtsi"
}
