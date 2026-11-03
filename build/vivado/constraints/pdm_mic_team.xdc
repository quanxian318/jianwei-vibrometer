# MP34DT01 PDM microphone on AX7010 J11-32/33 (Bank 35, 3.3 V) in the team's
# AN5642 design (bd/team_add_pdm.tcl). Same pins and names as the carrier
# design (AX7010_carrier_V1.10.xdc), where they reach J706 through J801.
set_property -dict {PACKAGE_PIN H16 IOSTANDARD LVCMOS33 DRIVE 8 SLEW SLOW} [get_ports mic_clk]
set_property -dict {PACKAGE_PIN G15 IOSTANDARD LVCMOS33} [get_ports mic_dat]

# The 2 MHz clock comes straight from a register; mic_dat passes a two-stage
# synchronizer. The data is taken SAMPLE_AT system clocks after the edge that
# opens its half period (default 18 of 25 at 100 MHz, about 160 ns at the
# pin), so the capture margin is set by that register (the microphone enables
# its output >= 18 ns after the opening edge, the sample is ~90 ns before the
# closing one) rather than by static timing against the PDM clock: there is
# no timing relationship to analyse, only the few-ns pin delays.
set_false_path -to [get_ports mic_clk]
set_false_path -from [get_ports mic_dat]
