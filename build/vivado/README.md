# 当前PL输入快照

来自最后生成printer_uart.bit.bin的printer_uart工程。双目计算核已从BD移除，保留的stereo相关RTL文件是原工程输入，不表示其被实例化。
`create_project.tcl`使用本目录相对路径重建；原工程的绝对C/D盘路径及运行缓存未作为依赖携入。
Nano仍走PS UART0，打印机走AXI UART16550（0x44050000）；Nano H20/J20/L16/L17，打印M17/D20/D19。
原始构建报告随附，移植脚本本次未经过Vivado重建验证。
