# Vivado 分项工程

由提供的 Vivado 测试工程归档整理，保留各工程的 XPR、BD、输入 RTL、约束、测试脚本、实现报告和位流。工程共享 build/vivado 中的 RTL、约束和用户 IP，XPR 文件已使用相对路径。

| 工程 | 用途 | 入口 |
|---|---|---|
| panel | 前面板静态电平测试 | panel/project/panel.xpr |
| sht40 | PS I²C 分支与 SHT40 读取 | sht40/project/sht40.xpr |
| lite | PS 辅助深度阶段的视频与外设工程 | lite/project/lite.xpr |
| nano_uart | Nano UART0 EMIO 接口 | nano_uart/project/nano_uart.xpr |
| printer_uart | 独立打印 UART 的采集与显示工程 | printer_uart/project/printer_uart.xpr |

panel 的静态测试使用 panel_probe.v；serial_source/panel_probe.v 保存串行 WS2812 测试源码。panel.bit.bin 对应串行测试，panel_static.bit.bin 对应静态电平测试，报告分别保存在 out 目录。两种测试共用灯光 GPIO，静态模式输出固定高低电平，串行模式发送五颗灯的 GRB 数据。

当前存档 XPR 对应静态测试。串行源代码和位流按各自文件保存。正常整机使用 board/artifacts/printer_uart.bit.bin，build/vivado 中保存其源码和实现结果。

sht40 的十次界面读数保存在 data/sht40_samples.json。archive_manifest.json 记录输入文件与归档文件的哈希，dependency_map.json 列出外部依赖与仓库内共享文件的对应关系。

在 Vivado 2025.2 中打开对应 XPR，可生成 IP 输出文件并执行综合与实现。各工程根目录和 build_scripts 保留原变换脚本，用于查阅工程演进过程。
