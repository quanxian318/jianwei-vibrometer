# PL、PS 与 ESP32 职责和接口分工

系统以 AX7010 的 PL 和 PS 为核心，Nano ESP32 负责辅助外设与网络网关。完整系统图、地址表和优化对比见[技术报告](../report/technical_report.pdf)。

## 测量数据流

AN5642 两路 OV5640 输出 DVP，PL 采集并转换为 AXI4-Stream，经 VDMA 写入 DDR。PS 读取图像，执行 12 测点与 4 参考点的 25×25 亚像素跟踪、静止参考平移补偿、128 帧 Hann FFT 和停机基线比较。代表测点波形给出主频、单轴峰峰值与有效值，结果送往本机界面、报警、存储、打印和 APP。

声学链路由 PL 提供 2 MHz PDM 时钟和原始码流 FIFO，PS 通过 CIC/FIR 抽取到 31.25 kHz 后进行包络分析，得到与视觉结果独立比较的主频。PS 辅助双目深度采用 160×90 Census 与两路径 SGM，约每秒更新一次。

## 单元与接口

| 单元 | 职责 | 接口 |
|---|---|---|
| PL | 双路视频采集、VDMA、HDMI、PDM 接收、补光 PWM、打印 UART | GP0 寄存器、HP0 存储访问 |
| PS | 多点测振、频谱与基线、声学分析、双目辅助深度、UI、记录、报警与打印 | AXI、I²C、UART、HTTP |
| Nano ESP32 | NFC、SD 语音、Wi-Fi/NTP、APP 网关与事件队列 | PS UART0 EMIO，115200、RTS/CTS |
| 前面板 | 旋转编码器、按键、5 颗 WS2812 状态灯 | 载板控制核 |
| APP | 手机与电脑界面、测量控制、历史记录和报警提示 | 局域网 HTTP 或 ESP32 热点网关 |

主载板通过两路 PS I²C 管理电源、RTC、IO 扩展与传感器。报警灯和蜂鸣器由 TCA9555 控制，打印使用独立 AXI UART16550。整机触屏、前面板、传感器、语音、NFC、报警及打印已按设计完成联调。

## 工程组织与优化成果

- [Vivado 工程](../build/vivado/README.md)保存采集、显示和外设基线工程及原始实现报告。
- [LK 与视觉验证](../sim/reference/vibration/README.md)记录跟踪算法与硬件累加核的比对。
- [双目验证](../sim/reference/stereo/README.md)记录原 PL 双目核与参考模型的比对。
- [板端运行](../board/README.md)说明镜像、设备树、配置和程序启动。
- [技能包](../skill/README.md)提供分工评审、接口核查、黄金比对和部署检查。
- [全部焊盘查询](../hardware/netlists/pin_browser.html)对应两板针序和电气网络。

双目深度从 PL 迁到 PS 后腾出视频与外设资源。当前归档 printer_uart 位流的 LUT 使用率为 50.82%，BRAM 为 50.83%，Slice 为 91.30%，建立时序裕量为 +0.756 ns。前面板控制、LK 累加和显示增强等 RTL 及测试平台位于 src/ax7010/pl。
