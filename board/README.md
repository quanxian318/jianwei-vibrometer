# 板端运行

系统使用 ALINX AX7010 的 512 MB DDR Linux 镜像。视频缓存占用 DDR 顶端 32 MiB，即 0x1E000000～0x1FFFFFFF。

## 准备镜像与加载 PL

从仓库根目录运行镜像处理脚本，将输出的 image.ub 放入启动分区后重启。

~~~sh
python3 src/ax7010/linux/vibmon/tools/prepare_an5642_image.py image_original.ub image.ub
~~~

板卡启动后停止相机与界面进程，以 root 加载 PL。

~~~sh
python3 src/ax7010/linux/vibmon/tools/prepare_an5642_runtime.py board/artifacts/printer_uart.bit.bin
~~~

脚本配置 FCLK1 为 125 MHz、FCLK2 为 40 MHz，开启两路 PS I²C 时钟，并通过 /dev/xdevcfg 加载位流。

## 板级配置

以 src/ax7010/linux/vibmon/config/current_board.example.json 为入口，核对实际路径后保存为 board.json。

| 项目 | 配置 |
|---|---|
| 相机 | camera.source = pl_vdma |
| 显示 | ui.display = pl_vdma |
| Nano | devices.uart_nano = /dev/ttyPS1，115200、8N1、RTS/CTS |
| 打印机 | devices.uart_printer = axi16550:0x44050000 |
| 标定 | measurement.calibration_file 指向板上的标定 JSON |
| 双目 | stereo.calibration_file 指向双目标定 JSON |
| 报警 | alarm.binary_threshold = true，alarm_peak_to_peak = 0.02 mm |

Nano 连接 PS UART0 EMIO，设备树和 UART 时钟配置启用 /dev/ttyPS1 后，程序按板级配置建立通信。

## 运行程序

复现实测程序快照。

~~~sh
gzip -dk board/artifacts/vibmond.gz
chmod +x board/artifacts/vibmond
board/artifacts/vibmond -c board.json
~~~

使用源码构建的程序。

~~~sh
sh build/build_ps.sh
.build/arm/vibmond -c board.json
~~~

源码构建程序通过标定文件的 layout_hash 将尺标比例与测点布局绑定。归档实测快照保留移动测量框后继续使用当前尺标比例的方式。复现快照中的行为时，在相同 ARM 构建配置下运行工具。

~~~sh
cd src/ax7010/linux/vibmon
python3 tools/reproduce_legacy_binary.py build_arm/vibmond release/vibmond --acknowledge-unbound-calibration
~~~

机位、测点深度或测量方向改变后重新标定；程序启动和测点布局改变后重新采集基线。源码新增的 result.thresholds.binary_threshold 字段保存当次报警模式。

[Nano 固件](../src/nano_esp32/README.md)和[APP 使用说明](../docs/app_guide.md)给出其余部署步骤。
