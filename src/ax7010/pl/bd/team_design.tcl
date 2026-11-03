# Board design of the current AX7010 setup: the team's AN5642 camera/HDMI
# project (11_队友Vivado工程_20261021, AX7010_Sdoira_PL_V1) with
#   team_bm_to_stereo.tcl  census+SGM stereo core and its DMA instead of the BM,
#   team_add_pdm.tcl       MP34DT01 PDM microphone receiver on J11-32/33,
#   team_build.tcl         implementation and bitstream.
#
# Vivado and Chinese paths do not mix: copy into one ASCII folder
#   <work>/AX7010_Sdoira_PL_V1   (whole folder from the team package)
#   <work>/AX7010_2023.1         (constraints and IP repository it references)
#   <work>/stereo_rtl/*.v        (../stereo/rtl)
#   <work>/audio_rtl/*.v         (../audio/rtl)
#   <work>/pdm_mic_team.xdc      (../constraints/pdm_mic_team.xdc)
#   <work>/team_*.tcl            (this folder)
# and run   vivado -mode batch -source <work>/team_design.tcl
# Result: <work>/out/design.bit, utilization*.rpt, timing.rpt; convert with
# linux/vibmon/tools/bit_to_bin.py for /dev/xdevcfg.
set root [file dirname [file normalize [info script]]]
open_project $root/AX7010_Sdoira_PL_V1/project/stereo_depth.xpr
open_bd_design [get_files design_1.bd]
source $root/team_bm_to_stereo.tcl
source $root/team_add_pdm.tcl
source $root/team_build.tcl
