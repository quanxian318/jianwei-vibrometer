# Adds the carrier-board subsystem (J11 / J801) to an existing AX7010 block
# design that already contains processing_system7 (e.g. the vendor AN5642
# camera/HDMI project). Run in the Vivado Tcl console with the project open:
#
#   source <...>/src/ax7010/pl/bd/add_carrier.tcl
#   carrier::add                 ;# uses the current block design
#
# Vivado cannot use paths with Chinese characters: copy ax7010/pl to an
# ASCII-only folder first (see ../README.md).
#
# Result: carrier_ctrl (module reference, AXI-Lite + IRQ), pdm_mic (module
# reference, AXI-Lite, no IRQ) for the MP34DT01 on J706, 2x AXI IIC,
# 2x AXI UART16550, 1x AXI Quad SPI, external ports named exactly as in
# AX7010_carrier_V1.10.xdc, interrupts appended to IRQ_F2P, fixed AXI
# addresses, and generated/carrier_generated.dtsi + carrier_map.json.

namespace eval carrier {
    variable dir [file dirname [file normalize [info script]]]
    # name  base        range
    variable addr_plan {
        carrier_ctrl_0 0x44000000 4K
        iic_mgmt       0x44010000 64K
        iic_sens       0x44020000 64K
        pdm_mic_0      0x44030000 4K
        uart_nano      0x44040000 64K
        uart_prn       0x44050000 64K
        spi_sens       0x44060000 64K
    }
    # Interrupt sources in the order they are appended to IRQ_F2P.
    variable irq_plan {
        carrier_ctrl_0 irq
        iic_mgmt       iic2intc_irpt
        iic_sens       iic2intc_irpt
        uart_nano      ip2intc_irpt
        uart_prn       ip2intc_irpt
        spi_sens       ip2intc_irpt
    }
}

proc carrier::latest_vlnv {name} {
    set defs [lsort [get_ipdefs -filter "NAME == $name"]]
    if {[llength $defs] == 0} { error "IP $name not found in the IP catalog" }
    return [lindex $defs end]
}

proc carrier::pin {cell name} {
    return [get_bd_pins -quiet "$cell/$name"]
}

proc carrier::tie {cell name value} {
    set p [carrier::pin $cell $name]
    if {$p eq ""} { return }
    if {[llength [get_bd_nets -quiet -of_objects $p]] > 0} { return }
    set c [create_bd_cell -type ip -vlnv [carrier::latest_vlnv xlconstant] "${cell}_${name}_tie"]
    set_property -dict [list CONFIG.CONST_WIDTH {1} CONFIG.CONST_VAL $value] $c
    connect_bd_net [get_bd_pins $c/dout] $p
}

proc carrier::connect_axi {ps cell intf} {
    set slave [get_bd_intf_pins $cell/$intf]
    set master "/$ps/M_AXI_GP0"
    # Rule keys differ between Vivado releases; try the current set first.
    if {[catch {apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
            -config [list Clk_master {Auto} Clk_slave {Auto} Clk_xbar {Auto} Master $master Slave "/$cell/$intf" intc_ip {Auto} master_apm {0}] $slave} err]} {
        puts "carrier: axi4 automation (new keys) failed: $err; retrying with legacy keys"
        apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config [list Master $master Clk {Auto}] $slave
    }
}

proc carrier::fclk_hz {ps} {
    set mhz [get_property -quiet CONFIG.PCW_ACT_FPGA0_PERIPHERAL_FREQMHZ $ps]
    if {$mhz eq "" || $mhz == 0} { set mhz [get_property CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ $ps] }
    return [expr {int(round($mhz * 1000000.0))}]
}

proc carrier::spi_ratio {clk_hz} {
    # Smallest power-of-two divider that keeps SCK at or below 1 MHz.
    set r 2
    while {[expr {double($clk_hz) / $r}] > 1000000.0 && $r < 2048} { set r [expr {$r * 2}] }
    return $r
}

proc carrier::append_irqs {ps sources} {
    set irq_pin [get_bd_pins -quiet $ps/IRQ_F2P]
    if {$irq_pin eq ""} {
        set_property -dict [list CONFIG.PCW_USE_FABRIC_INTERRUPT {1} CONFIG.PCW_IRQ_F2P_INTR {1}] [get_bd_cells $ps]
        set irq_pin [get_bd_pins $ps/IRQ_F2P]
    }
    set net [get_bd_nets -quiet -of_objects $irq_pin]
    set concat ""
    set first 0
    if {$net ne ""} {
        set drv [get_bd_pins -quiet -of_objects $net -filter {DIR == O}]
        set drv_cell [get_bd_cells -quiet -of_objects $drv]
        if {$drv_cell ne "" && [string match "*:xlconcat:*" [get_property VLNV $drv_cell]]} {
            set concat $drv_cell
            set first [get_property CONFIG.NUM_PORTS $concat]
        } else {
            # A single interrupt source already drives IRQ_F2P: move it to In0 of a new concat.
            disconnect_bd_net $net $irq_pin
            set concat [create_bd_cell -type ip -vlnv [carrier::latest_vlnv xlconcat] carrier_irq_concat]
            set_property CONFIG.NUM_PORTS 1 $concat
            connect_bd_net $drv [get_bd_pins $concat/In0]
            connect_bd_net [get_bd_pins $concat/dout] $irq_pin
            set first 1
        }
    } else {
        set concat [create_bd_cell -type ip -vlnv [carrier::latest_vlnv xlconcat] carrier_irq_concat]
        set_property CONFIG.NUM_PORTS 1 $concat
        connect_bd_net [get_bd_pins $concat/dout] $irq_pin
        set first 0
    }
    set n [expr {[llength $sources] / 2}]
    if {$first + $n > 16} { error "IRQ_F2P has only 16 inputs ($first already used, $n needed)" }
    set_property CONFIG.NUM_PORTS [expr {$first + $n}] $concat
    set map {}
    set idx $first
    foreach {cell pin} $sources {
        connect_bd_net [get_bd_pins $cell/$pin] [get_bd_pins $concat/In$idx]
        lappend map $cell $idx
        incr idx
    }
    return $map
}

proc carrier::dt_irq {idx} {
    # IRQ_F2P[0..7] -> GIC SPI 61..68, [8..15] -> SPI 84..91; DT uses SPI-32.
    if {$idx < 8} { return [expr {29 + $idx}] }
    return [expr {52 + $idx - 8}]
}

proc carrier::add {} {
    variable dir
    variable addr_plan
    variable irq_plan

    set bd [current_bd_design -quiet]
    if {$bd eq ""} { error "open the block design first (open_bd_design ...)" }
    set ps_cell [get_bd_cells -filter {VLNV =~ "xilinx.com:ip:processing_system7:*"}]
    if {[llength $ps_cell] != 1} { error "expected exactly one processing_system7 cell" }
    set ps [get_property NAME $ps_cell]
    foreach {name base range} $addr_plan {
        if {[get_bd_cells -quiet $name] ne ""} { error "cell $name already exists; carrier subsystem was added before" }
    }

    # Sources and constraints.
    set rtl [file normalize "$dir/../rtl"]
    add_files -norecurse [glob $rtl/*.v]
    add_files -norecurse [file normalize "$dir/../audio/rtl/pdm_mic.v"]
    update_compile_order -fileset sources_1
    add_files -fileset constrs_1 -norecurse [file normalize "$dir/../constraints/AX7010_carrier_V1.10.xdc"]
    add_files -fileset constrs_1 -norecurse [file normalize "$dir/../constraints/carrier_timing.xdc"]

    set clk_hz [carrier::fclk_hz $ps_cell]
    puts "carrier: fabric clock FCLK_CLK0 = $clk_hz Hz"

    # Cells.
    create_bd_cell -type module -reference carrier_ctrl carrier_ctrl_0
    set_property CONFIG.CLK_HZ $clk_hz [get_bd_cells carrier_ctrl_0]
    create_bd_cell -type module -reference pdm_mic pdm_mic_0
    set_property CONFIG.CLK_HZ $clk_hz [get_bd_cells pdm_mic_0]
    foreach c {iic_mgmt iic_sens} {
        set cell [create_bd_cell -type ip -vlnv [carrier::latest_vlnv axi_iic] $c]
        set_property -dict [list CONFIG.IIC_FREQ_KHZ {100}] $cell
    }
    foreach c {uart_nano uart_prn} {
        create_bd_cell -type ip -vlnv [carrier::latest_vlnv axi_uart16550] $c
    }
    set spi [create_bd_cell -type ip -vlnv [carrier::latest_vlnv axi_quad_spi] spi_sens]
    set ratio [carrier::spi_ratio $clk_hz]
    set_property -dict [list CONFIG.C_SPI_MODE {0} CONFIG.C_NUM_SS_BITS {2} CONFIG.C_USE_STARTUP {0} \
        CONFIG.C_FIFO_DEPTH {16} CONFIG.C_SCK_RATIO $ratio] $spi
    puts "carrier: SPI SCK = [expr {$clk_hz / $ratio}] Hz (ratio $ratio)"

    # AXI connections (clock/reset come from the automation).
    carrier::connect_axi $ps carrier_ctrl_0 s_axi
    carrier::connect_axi $ps pdm_mic_0 s_axi
    foreach c {iic_mgmt iic_sens uart_nano uart_prn} { carrier::connect_axi $ps $c S_AXI }
    carrier::connect_axi $ps spi_sens AXI_LITE
    connect_bd_net [get_bd_pins carrier_ctrl_0/s_axi_aclk] [get_bd_pins spi_sens/ext_spi_clk]

    # IP <-> carrier_ctrl signals.
    foreach {c n} {iic_mgmt 0 iic_sens 1} {
        foreach s {scl sda} {
            connect_bd_net [get_bd_pins $c/${s}_o] [get_bd_pins carrier_ctrl_0/iic${n}_${s}_o]
            connect_bd_net [get_bd_pins $c/${s}_t] [get_bd_pins carrier_ctrl_0/iic${n}_${s}_t]
            connect_bd_net [get_bd_pins carrier_ctrl_0/iic${n}_${s}_i] [get_bd_pins $c/${s}_i]
        }
    }
    connect_bd_net [get_bd_pins uart_nano/sout] [get_bd_pins carrier_ctrl_0/uart_nano_sout]
    connect_bd_net [get_bd_pins carrier_ctrl_0/uart_nano_sin] [get_bd_pins uart_nano/sin]
    connect_bd_net [get_bd_pins uart_nano/rtsn] [get_bd_pins carrier_ctrl_0/uart_nano_rtsn]
    connect_bd_net [get_bd_pins carrier_ctrl_0/uart_nano_ctsn] [get_bd_pins uart_nano/ctsn]
    connect_bd_net [get_bd_pins uart_prn/sout] [get_bd_pins carrier_ctrl_0/uart_prn_sout]
    connect_bd_net [get_bd_pins carrier_ctrl_0/uart_prn_sin] [get_bd_pins uart_prn/sin]
    connect_bd_net [get_bd_pins carrier_ctrl_0/uart_prn_ctsn] [get_bd_pins uart_prn/ctsn]
    foreach c {uart_nano uart_prn} {
        foreach p {dcdn dsrn rin ri} { carrier::tie $c $p 1 }
    }
    connect_bd_net [get_bd_pins spi_sens/sck_o] [get_bd_pins carrier_ctrl_0/spi_sck_o]
    connect_bd_net [get_bd_pins spi_sens/io0_o] [get_bd_pins carrier_ctrl_0/spi_io0_o]
    connect_bd_net [get_bd_pins carrier_ctrl_0/spi_io1_i] [get_bd_pins spi_sens/io1_i]
    connect_bd_net [get_bd_pins spi_sens/ss_o] [get_bd_pins carrier_ctrl_0/spi_ss_o]
    connect_bd_net [get_bd_pins spi_sens/ss_t] [get_bd_pins carrier_ctrl_0/spi_ss_t]
    carrier::tie spi_sens spisel 1
    # PFAIL stops the microphone clock in hardware before 3V3_SENS goes.
    connect_bd_net [get_bd_pins carrier_ctrl_0/pfail_kill] [get_bd_pins pdm_mic_0/kill]

    # External ports with the XDC names.
    foreach p {led1_pwm led2_pwm ws_data esp_txd esp_rts prn_txd adxl_cs imu_cs spi_sclk spi_mosi} {
        create_bd_port -dir O $p
        connect_bd_net [get_bd_ports $p] [get_bd_pins carrier_ctrl_0/$p]
    }
    foreach p {k1 tof_int rtc_1hz pfail_n exp_int_n tach_in enc_sw enc_a enc_b esp_rxd esp_cts esp_irq \
               prn_rxd prn_cts adxl_drdy imu_int1 imu_int2 spi_miso} {
        create_bd_port -dir I $p
        connect_bd_net [get_bd_ports $p] [get_bd_pins carrier_ctrl_0/$p]
    }
    foreach p {i2c0_scl i2c0_sda i2c1_scl i2c1_sda} {
        create_bd_port -dir IO $p
        connect_bd_net [get_bd_ports $p] [get_bd_pins carrier_ctrl_0/$p]
    }
    create_bd_port -dir O mic_clk
    connect_bd_net [get_bd_ports mic_clk] [get_bd_pins pdm_mic_0/mic_clk]
    create_bd_port -dir I mic_dat
    connect_bd_net [get_bd_ports mic_dat] [get_bd_pins pdm_mic_0/mic_dat]

    # Interrupts.
    set irq_map [carrier::append_irqs $ps $irq_plan]

    # Fixed addresses so the Linux configuration does not change between builds.
    set space [get_bd_addr_spaces $ps/Data]
    set addr_out {}
    foreach {name base range} $addr_plan {
        set seg [get_bd_addr_segs -quiet -of_objects $space -filter "NAME =~ *${name}*"]
        if {[llength $seg] != 1} {
            puts "carrier: WARNING no unique address segment for $name (found: $seg)"
            continue
        }
        if {[catch {set_property range $range $seg; set_property offset $base $seg} err]} {
            puts "carrier: WARNING could not place $name at $base: $err"
        }
        lappend addr_out $name [format 0x%08X [get_property OFFSET $seg]]
    }

    validate_bd_design
    save_bd_design
    carrier::write_outputs $clk_hz $ratio $addr_out $irq_map
    puts "carrier: done. Regenerate the HDL wrapper (make_wrapper) if the project uses the generated one;"
    puts "carrier: a hand-written top level needs the 34 new ports added by hand."
}

proc carrier::write_outputs {clk_hz ratio addr_out irq_map} {
    variable dir
    set gen [file normalize "$dir/generated"]
    file mkdir $gen
    array set addr $addr_out
    array set irq $irq_map

    set f [open "$gen/carrier_map.json" w]
    puts $f "\{"
    puts $f "  \"fclk_hz\": $clk_hz,"
    puts $f "  \"spi_sck_hz\": [expr {$clk_hz / $ratio}],"
    puts $f "  \"ip\": \{"
    set names {carrier_ctrl_0 iic_mgmt iic_sens pdm_mic_0 uart_nano uart_prn spi_sens}
    set i 0
    foreach n $names {
        if {[info exists addr($n)]} { set a "\"$addr($n)\"" } else { set a "null" }
        if {[info exists irq($n)]} { set q $irq($n) } else { set q "null" }
        if {$i < [llength $names] - 1} { set sep "," } else { set sep "" }
        puts $f "    \"$n\": \{\"axi_base\": $a, \"irq_f2p\": $q\}$sep"
        incr i
    }
    puts $f "  \}"
    puts $f "\}"
    close $f

    set f [open "$gen/carrier_generated.dtsi" w]
    puts $f "/* Generated by add_carrier.tcl - include from system-user.dtsi */"
    if {[info exists addr(carrier_ctrl_0)] && [info exists irq(carrier_ctrl_0)]} {
        set base $addr(carrier_ctrl_0)
        puts $f "&amba_pl \{"
        puts $f "\tcarrier_uio: carrier-ctrl@[string tolower [string range $base 2 end]] \{"
        puts $f "\t\tcompatible = \"vibmon,carrier-ctrl\", \"vibmon,uio\";"
        puts $f "\t\treg = <$base 0x1000>;"
        puts $f "\t\tinterrupt-parent = <&intc>;"
        puts $f "\t\tinterrupts = <0 [carrier::dt_irq $irq(carrier_ctrl_0)] 4>;"
        puts $f "\t\};"
        puts $f "\};"
    }
    close $f
    puts "carrier: wrote $gen/carrier_map.json and carrier_generated.dtsi"
}
