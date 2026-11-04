# Jianwei 见微视觉测振仪

基于 ALINX AX7010（AMD Zynq-7000 XC7Z010）的非接触多点视觉测振仪。双目相机、自研主载板、前面板和 Nano ESP32 集成在 3D 打印外壳内，完成采集、测振、声学核对、声光与语音报警、热敏打印及手机/电脑查看。

2026 AMD 自主选题赛道初级组。开发工具为 Vivado 2025.2，PS 使用 Linux C++17。

## 作品资料

- [技术报告](report/technical_report.pdf) / [LaTeX 源文件](report/source/main.tex)
- [完整演示视频](media/jianwei_demo.mp4)：179.633 秒，1920×1080，H.264 + AAC，约 65.4 MB
- [英文海报](media/poster_en.pptx) / [PDF](media/poster_en.pdf)
- [中文海报](media/poster_zh.pptx) / [PDF](media/poster_zh.pdf)
- [全部代码 RAR](packages/jianwei_code.rar)
- [大模型协作记录](report/interaction_records.md) / [可复用技能包](skill/README.md)

## 系统与实测

PL 负责双路相机采集、VDMA/HDMI、PDM 麦克风接收、补光 PWM 和打印 UART。PS 运行最多 12 测点与 4 参考点的亚像素跟踪、停机基线、128 帧 Hann FFT、声学包络谱、界面和记录。Nano ESP32 负责 NFC、SD 语音与 Wi-Fi 网关。Android、iOS、Windows 共用 APP 界面。

| 2026-11-04 板端实测 | 结果 |
|---|---:|
| 电机转速 / 理论转频 | 1200 r/min / 20 Hz |
| 视觉主频 | 19.9476 Hz |
| 视觉主频与理论转频相对差 | 约 0.26% |
| 声学主频 | 19.9277 Hz |
| 位移峰峰值 / 有效值 | 0.2090 / 0.0701 mm |
| 采集 | 452 帧，45.0477 fps，0 丢帧 |
| 有效测点 / 窗口 | 12/12，11/11 |
| LUT / FF / BRAM | 50.82% / 40.97% / 50.83% |
| 建立时序裕量 | +0.756 ns（已约束路径） |

原始数据：[measurement_20261104.json](data/measurement_20261104.json)。毫米值采用 0.203085 mm/px 尺标比例；当前视觉采样的奈奎斯特频率约为 22.5 Hz。双目深度在 PS 以 160×90 分辨率辅助计算。

## 目录与赛题提交项

| 目录 | 内容 |
|---|---|
| `src/` | PS、RTL、Nano、APP、共用协议、PC 模拟源码 |
| `sim/` | 主机和 RTL 验证入口、黄金向量相关结果、优化参考数据 |
| `build/` | 构建脚本、当前 Vivado BD/IP/约束、原始综合与实现报告 |
| `board/` | 位流、PS 可执行快照与板端操作说明 |
| `data/` | 实测记录、资源与时序数据 |
| `skill/` | 工作流、案例、检查脚本及通用 PYNQ Overlay 技能 |
| `report/` | 技术报告、LaTeX、协作记录与技能提炼过程 |
| `hardware/` | EDA 原生工程、BOM、Gerber、网表、原理图、外壳 CAD/STL |
| `docs/` | 接线、引脚、协议、模块设置、散斑打印与当前构建说明 |
| `media/` | 海报、视频、产品图片 |
| `packages/` | 代码 RAR 与文件校验清单 |

文档采用中文正文，目录和文件名使用 ASCII 英文名称，保留各工具链要求的标准工程文件名。

## 复现

### 1. 接线与板卡

使用 AX7010、AN5642 双 OV5640、Nano ESP32 及自研两块 PCB。按 [接线表](docs/wiring.md)、[FPGA 针序](docs/fpga_pins.csv) 和 [模块设置](docs/module_switches.md) 连接。PCB 工程位于 `hardware/pcb/jianwei.epro2`，最终打印件位于 `hardware/enclosure/print/`。

### 2. 主机与 RTL 测试

在 Linux/WSL 中准备 g++、make、Python 3 和 Icarus Verilog，从仓库根目录运行：

```sh
sh sim/run_host.sh
sh sim/run_rtl.sh
python3 skill/run_offline.py .build/skill_checks
```

测试输出位于 `.build`；本次归档结果在 `sim/results`。

### 3. PL 与 PS 构建

```sh
cd build/vivado
vivado -mode batch -source create_project.tcl -tclargs build
cd ../..
sh build/build_ps.sh
```

PS 交叉编译器为 `arm-linux-gnueabihf-g++`/`gcc`，结果为 `.build/arm/vibmond`。当前 BD 使用厂商 IP，Vivado 须有对应器件/IP 支持。

### 4. 部署与测量

按 [板端说明](board/README.md) 配置预留内存、加载位流和运行程序。Nano 与 APP 分别参见 [Nano](src/nano_esp32/README.md)、[APP](src/mobile_app/README.md)。固定机位并放置散斑和静止参考点，标定后先采约 15 秒停机基线，再测量约 10 秒；可在屏幕或 APP 查看结果、控制打印。

## 许可

项目原创部分采用 [MIT](LICENSE)。厂商 IP、参考模型及其他第三方材料保留原许可，见 [第三方说明](THIRD_PARTY_NOTICES.md)。

分项 Vivado 工程入口见 [Vivado 测试工程](build/vivado_tests/README.md)。
