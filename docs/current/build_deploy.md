# 构建与部署

## PS

从仓库根目录构建 ARM 程序并运行主机验证。

~~~sh
sh build/build_ps.sh
sh sim/run_host.sh
~~~

ARM 工具链为 arm-linux-gnueabihf-g++，使用 -O2 -mfpu=neon -mfloat-abi=hard，静态链接，输出为 .build/arm/vibmond。

## PL

~~~sh
cd build/vivado
vivado -mode batch -source create_project.tcl -tclargs build
~~~

器件为 xc7z010clg400-1，工具为 Vivado 2025.2。脚本加载 BD、RTL、XDC 与用户 IP。实现报告位于 build/vivado/reports_original_build，配套位流位于 board/artifacts/printer_uart.bit.bin。

## 板端与配置

[板端说明](../../board/README.md)列出 Linux 镜像设备树处理、DDR 预留、PL 加载、Nano UART 配置以及程序启动命令。

源码构建通过 layout_hash 绑定毫米标定，归档实测快照采用移动测量框后保留尺标比例的方式。两种运行方式及对应命令见板端说明。新生成记录在 result.thresholds 中保存 binary_threshold、warn_peak_to_peak 和 alarm_peak_to_peak。

## Nano 与 APP

- [Nano 固件](../../src/nano_esp32/README.md)
- [APP 构建与安装](../app_guide.md)
- [板端实测记录](../../data/measurement_20261104.json)
