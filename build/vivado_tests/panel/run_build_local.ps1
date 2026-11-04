param([string]$Script = 'build.tcl')
$ErrorActionPreference = 'Stop'
$toolRoot = 'E:\Xilinx2025\2025.2\Vivado'
$env:PROCESSOR_ARCHITECTURE='AMD64'
$env:XILINX_VIVADO=$toolRoot
$env:RDI_APPROOT=$toolRoot
$env:HDI_APPROOT=$toolRoot
$env:RDI_BASEROOT='E:\Xilinx2025\2025.2'
$env:RDI_DATADIR="$toolRoot\data"
$env:RDI_LIBDIR="$toolRoot\lib\win64.o"
$env:RDI_BINDIR="$toolRoot\bin"
$env:RDI_BINROOT="$toolRoot\bin"
$env:RDI_PLATFORM='win64'
$env:RDI_OPT_EXT='.o'
$env:TCL_LIBRARY="$toolRoot/tps/tcl/tcl8.6"
$env:PATH="$toolRoot\lib\win64.o;$toolRoot\bin;$toolRoot\tps\win64\jre21.0.5_11\bin\server;$toolRoot\tps\win64\jre21.0.5_11\bin;$toolRoot\tps\win64\python-3.13.0;$toolRoot\tps\mingw\10.0.0\win64.o\nt\bin;"+$env:PATH
Set-Location $PSScriptRoot
& "$toolRoot\bin\vivado.bat" -mode batch -source $Script -log "$Script.log" -journal "$Script.jou"
exit $LASTEXITCODE
