# 双目深度改进：PL 双目核（Census＋SGM＋左右校验）、标定校正与 PS 后处理

日期：2026/10/21。按[分工文档](../../../docs/partition.md)的"双目空间信息"一行：**PL 做双目匹配，输出视差及有效性；PS 做标定、校正配置、视差到深度换算**。对照对象为队友工程 `11_队友Vivado工程_20261021` 里的 sdoira PL BM（原包未改动）。没有上板，没有用 AN5642 实拍双目图验证；Vivado 综合实现在 2026/10/22 补做，见“还没做的”第 1 条。

## 结论

1. **新 PL 双目核比队友 BM 准得多。** 在 9 个带真值的 Middlebury 场景上（D=64），模拟两只 OV5640 各自自动曝光（增益 0.8、偏置 +12、gamma 1.15、噪声 σ=2）：

   | 方案 | 测量像素密度 | 误差 >2 px 的比例 | RMS 误差 |
   |---|---:|---:|---:|
   | 队友 PL BM＋V1.2 空间支持滤波（软件复现） | 77.3% | 8.2% | 5.42 px |
   | 新 PL 核原始输出（与 RTL 逐位一致） | 90.9% | 4.4% | 2.10 px |
   | 新 PL 核＋PS 后处理（vibmon 默认） | 87.0% | 3.1% | 1.46 px |
   | 同上，显示图（含填补像素） | 95.2% | 4.3% | — |

   理想图像下：BM 为 89.1% / 5.8% / 3.97 px，新方案为 89.6% / 2.7% / 1.34 px（显示图 96.0% / 3.8%）。再叠加 0.75 px 垂直错位（模拟校正残差）：BM 为 69.4% / 11.2% / 6.40 px，新方案为 79.8% / 6.6% / 1.94 px。各场景明细见 [benchmark_results.json](benchmark_results.json)。
2. **黑点和噪点。**
   - 黑点：PS 端按行填补小孔，两端视差一致时插值，深度台阶处按遮挡规则取背景值。显示密度从 87% 提到 95%。填补像素单独标记，不参与测量、尺度换算和触屏点测距。
   - 噪点：靠代价聚合、唯一性、左右一致性、中值和斑点滤除压低错误匹配；另有帧间一致性滤波，一个值需连续两帧在 1 px 内一致才显示，抑制静止场景的闪烁。
   - 图：[Cones](figures/cones_exposure.png)、[Teddy](figures/teddy_exposure.png)、[Tsukuba](figures/tsukuba_exposure.png)、[Motorcycle](figures/motorcycle_exposure.png)。品红为误差 >2 px，深灰为无效。
3. **亚像素偏差从 0.09–0.13 px 降到 0.002 px。** 抛物线或 V 形插值在 SGM 代价上有"像素锁定"；PS 端对每个有效像素做一维 LK 细化（7×7 窗、增益/偏置归一化），数学与位移核 `lk_accum` 相同。视差 18 px 时，深度的系统误差从约 0.5–0.7% 降到约 0.01%。
4. **PL 核资源只有 BM 的一部分。** 用 yosys synth_xilinx 粗估（不是 Vivado 结果），MAXW=320、D=64：

   | 指标 | 新 PL 核 | 队友 BM（独立综合） |
   |---|---:|---:|
   | 逻辑 LUT | 约 2.0k | 6605 |
   | 分布式 RAM 所用 LUT | 约 1.1k | （含在上一行） |
   | FF | 约 1.5k | 4442 |
   | BRAM36 | 5.5 | 17 |
   | DSP | 4 | 1 |

   每像素 67.2 时钟；320×180 约 3.87 M 时钟，100 MHz 下约 38.7 ms（约 26 fps），与 BM 实测的 25–26 fps 相当。
5. **标定是最大的一环。** 垂直错位 0.75 px 就让所有方法的坏点率翻倍。队友的工程只做了"整体垂直偏移"近似。现在提供 PC 端棋盘格标定工具和板端校正映射：C++ 映射与 OpenCV `initUndistortRectifyMap` 逐像素一致，合成自检恢复焦距误差 0.002%、基线 60.004 mm（真值 60 mm）。

## 接入总框架（2026/10/21 第二轮）

双目已按载板控制的同一套路接进整机：块设计脚本固定地址与中断并生成设备树；Linux 用 UIO 按 AXI 基地址找设备；应用由配置启用服务。

| 层 | 内容 | 位置 |
|---|---|---|
| PL 封装 | `stereo_axi`：AXI4-Lite 寄存器（ID、CONFIG、宽高、START、STATUS、IRQ_EN、CYCLES、FRAMES）；AXI4-Stream 输入（DMA MM2S，每像素 16 位 {左, 右}）与输出（DMA S2MM，每像素 16 位视差，帧末 tlast）；512 项输出 FIFO 与溢出标志；DONE 中断 | `ax7010/pl/stereo/rtl/stereo_axi.v` |
| 块设计 | `add_stereo.tcl`：在 `carrier::add` 之后执行，加入 `stereo_0` 与 AXI DMA（简单模式，16 位流，23 位长度），内存侧接 PS S_AXI_HP2；寄存器 0x4407_0000（核）、0x4408_0000（DMA）；中断追加到 IRQ_F2P；写出 `generated/stereo_generated.dtsi`（保留 DDR 1 MiB @0x1F000000 ＋ UIO 节点：map0 核、map1 DMA、map2 缓冲）与 `stereo_map.json` | `ax7010/pl/bd/add_stereo.tcl` |
| 设备树/内核 | 载板与双目两个 UIO 节点共用 compatible `vibmon,uio`，内核命令行改为 `uio_pdrv_genirq.of_id=vibmon,uio`（`add_carrier.tcl` 生成的节点同步加了该 compatible） | `petalinux/system-user.dtsi`、`src/README.md` |
| PS 驱动 | `PlStereo`：校验 ID/版本和 CONFIG 与 `plCoreParams()` 一致；按 32 位字写入非缓存缓冲；按"核 START → S2MM → MM2S"的顺序编程；等待中断（无中断时轮询）；检查 DMA 错误和 FIFO 溢出 | `linux/vibmon/src/drv/stereo_pl.*`、`stereo_regs.h` |
| 服务/应用 | `backend=pl` 时先探测 PL 设备：找到就用 PL，`DepthMap.backend="PL"`；单帧失败时回落到 PS 模型并计数；`pl_verify_every=N` 时每 N 帧在 PS 上用模型复算，比对 PL 输出（板上 PS/PL 一致性自检）。`status` 输出 backend、pl_frames、pl_failures、pl_verified、pl_mismatch_frames、pl_cycles；测量记录新增 `stereo` 段（后端、标定 id、ROI 深度、是否用于尺度）；视觉页标题显示"PL / PS 软件计算"和有效比例 | `src/stereo/stereo_service.*`、`src/app/*`、`src/ui/ui_vision.cpp` |

**PS/PL 一致性证据链**
- 寄存器表：`stereo_regs.h` 与 `stereo_axi.v` 的地址、ID、版本和固定算法参数由单元测试自动比对。
- RTL 侧：`tb_stereo_axi` 按驱动的编程方式访问寄存器，输入输出两侧都加随机停顿，连续两帧逐位一致，并检查中断和 tlast。
- PS 侧：`test_stereo_pl` 用仿真的封装层＋DMA 跑驱动，输出与模型逐位一致；同时覆盖 CONFIG 不符、FIFO 溢出、超时和尺寸非法几种出错情况。
- 板上：`pl_verify_every` 定期复算比对。

**资源**（yosys 粗估，含封装层，不含 AXI DMA IP）：约 2.2k 逻辑 LUT ＋ 1.3k 分布式 RAM LUT、1.8k FF、5.5 BRAM36、4 DSP。

## 队友 BM 效果差的原因

- **没有立体校正**：只估一个整体垂直偏移，镜头畸变和两相机的相对旋转都没处理。
- **代价对曝光敏感**：在截断 X-Sobel 上算 7×7 SAD，两只相机自由运行、自动曝光不同时，代价被系统性抬高。新方案用 Census（只比较像素大小关系），不受增益、偏置影响。
- **没有代价聚合**：弱纹理和重复纹理处的匹配全靠 7×7 窗，容易出错。新方案用 SGM 路径聚合（P1/P2 平滑约束，P2 随灰度边缘自适应）。
- **没有左右一致性校验**：遮挡区和错误匹配留在输出里。我们的消融实验显示，去掉左右校验后坏点率翻倍（2.7% → 5.5%）。
- **亚像素插值粗**：V 形插值的偏差约 0.1 px，有像素锁定。

## 分工与数据流

```
左/右 OV5640 ─▶ PS：标定校正映射＋按整数倍面积降采样到 320×180（StereoService）
            ─▶ PL：stereo_core = census_cs97（9×7 中心对称 Census，行缓存）
                               → sgm_core（Hamming 代价、2 路径 SGM、WTA、唯一性、V 形亚像素、左右校验）
            ─▶ PS：纹理门限、LK 亚像素细化、3×3 中值、斑点滤除、小孔填补（标记）、帧间一致性
            ─▶ 深度 Z = f·B/d → Application::publishDepth()（视觉页）；可选给测振提供 mm/px = Z/fx
```

- **配置**（`vibmon.json` 的 `stereo` 段，默认 `enabled=false`）：
  - `backend: "pl"` 时参数固定为与 RTL 一致。在 AXI DMA 封装和 Linux 驱动完成前，这条路径在 PS 上运行逐位一致的模型，状态和深度图里标 `PL-model`。
  - `backend: "ps"` 是可配置的软件 SGM（4/8 路径），作参考。
  - 没有有效标定文件时不发布毫米深度，避免把未标定视差当成毫米显示。
- **测振尺度**：`measurement.scale_source` 可选 `file` / `stereo` / `file_or_stereo`。取测点 ROI 内测量像素（不含填补像素）深度的中位数 Z，按 Z/fx 换算 mm/px；记录里的 calibration id 写成 `stereo:<标定id>:z<mm>`。前提是测点表面大致正对相机（路线 §7）。默认 `file`，行为不变。
- **PS 与 ESP32**：协议 V1 不承载深度数据，ESP32 侧无改动。

## 新增文件

| 层 | 文件 | 内容 |
|---|---|---|
| PL | `src/ax7010/pl/stereo/rtl/census_cs97.v`、`sgm_core.v`、`stereo_core.v` | 完整双目核，流式 valid/ready 接口：输入 {左灰度, 右灰度}，输出 Q4 视差，0xFFFF 为无效 |
| PL 验证 | `pl/stereo/sim/`、`pl/stereo/vectors/` | `sgm_core`（Census 入）与 `stereo_core`（灰度入）在合成台阶 96×40、Cones 实景裁剪 112×48 上逐位一致（Icarus 12，`sh pl/stereo/sim/run_tests.sh`） |
| PS | `linux/vibmon/src/stereo/stereo_match.*` | Census、`sgmRaw`（PL 段模型）、`stereoPostProcess`、`matchSgm`、队友 BM 软件复现 `matchBmBaseline`、一维 LK 细化 |
| PS | `src/stereo/stereo_rectify.*` | 标定 JSON 读取、校正映射（与 OpenCV 一致）、降采样＋双线性重映射 |
| PS | `src/stereo/stereo_service.*` | 左右帧按时间戳配对（软件配对，超过 8 ms 拒绝）、PL/PS 后端、帧间一致性滤波、发布深度、ROI 尺度换算 |
| PS 应用 | `src/app/*`、`config/vibmon.json` | `stereo` 配置段、右相机、左帧分流（测量采集期间暂停）、`status` 输出双目统计、`scale_source` |
| 工具 | `tools/stereo_eval.cpp`、`tools/sgm_vectors.cpp` | 评测与校正映射导出；PL 黄金向量 |
| PC | `pc_sim/stereo_benchmark.py`、`pc_sim/stereo_calibrate.py` | 真值评测；棋盘格标定（含 `--self-test`） |
| 测试 | `tests/test_stereo.cpp` | 平面、亚像素偏差、台阶＋曝光差、无纹理拒绝、帧间滤波、标定加载、服务输出毫米深度与尺度 |

## 复现

```sh
cd src/ax7010/linux/vibmon && make test && make integration-test
sh src/ax7010/pl/stereo/sim/run_tests.sh                    # PL 逐位比对（Icarus）
python3 src/pc_sim/stereo_calibrate.py --self-test <输出目录>  # 标定工具自检（OpenCV）
# 真值评测：数据来自 opencv_extra testdata/cv/stereomatching/datasets（8 场景）与 scikit-image stereo_motorcycle
python3 src/pc_sim/stereo_benchmark.py <build>/stereo_eval <数据目录> out.json --save-maps <目录>
```

本次在 Linux x86-64（g++ 13）上：8 组单元测试（新增 stereo 组）、36 项应用集成检查、冒烟测试通过，编译无警告；Icarus 12 下 4 项 RTL 比对全部逐位一致，lk_accum 和 carrier_ctrl 测试平台仍通过。

## 还没做的

1. **PL 上板**：
   - 已在队友 AN5642 工程里用 `ax7010/pl/bd/team_bm_to_stereo.tcl` 去掉 BM、接入本核与 AXI DMA（HP1，核 0x43C2_0000、DMA 0x4040_0000），并由 `team_design.tcl` 与 MP34DT01 麦克风接收核一起综合实现（Vivado 2025.2）。DMA 存储侧为 64 位，HP1 与本核同用 40 MHz，互连不需要时钟和位宽转换（slice 已基本用满，这样才放下接收核）。全设计 13.5k LUT（76.7%）、slice 98.6%、41.5 BRAM36（69%）、3 DSP；WNS +0.462 ns、WHS +0.007 ns，无失败端点，40 MHz 域最长路径 22.8 ns（余量 1.155 ns）；位流与报告在 `src/ax7010/pl/stereo/bitstream/`（`an5642_stereo_sgm40_pdm.bit.bin` 由 `prepare_an5642_runtime.py` 加载，该脚本同时把 FCLK2 设为 40 MHz）。100 MHz 时 SGM 扫描（路径代价读出→最小值与比较→右视差缓冲写使能，22 级逻辑）约 19.4 ns，WNS −10.2 ns，所以本核和 DMA 改用 FCLK2 40 MHz，256×144 约 61 ms/对；回到 100 MHz 需要给扫描加流水并重做逐位比对。该段记录对应早期 PL 双目阶段，当前整机分工见技术报告。
   - 导出 XSA、构建 PetaLinux，用 `pl_verify_every` 观察 `pl_mismatch_frames` 是否为 0。
   - 与采集、显示、`lk_accum`、视觉显示链一起排资源：当前已用 41.5 个 BRAM36（含麦克风 FIFO 8 个）、slice 98.6%；位移核按 32×32 模板约 4–5 个 BRAM36、显示链 160×90 约 22 个，合计超过 60，接入前需要缩小麦克风 FIFO 或显示链工作图，并腾出 slice。
2. **AN5642 实拍标定**：
   - 2026/10/22 用一对实拍图（左 cam1、右 cam0）做块匹配：右目整行低约 30 px、各处一致（未见明显旋转）；远景视差 27–30 px，而 3.6 mm 镜头、40 mm 基线在 10 m 处只应约 5 px，说明两镜头水平偏心约 22 px，名义标定会把 10 m 显示成约 2 m、远近关系仍对。名义文件已改为 f 1286 px、右目主点 (618, 390)；剩余误差用已知距离校正（视觉页“距离校正”或 `stereo_distance`）。同一对图上 320×180 实测 35%、补洞 11%，余下的无效区主要是超出搜索范围的近物、白色桌面和左侧搜索边带；放宽去斑会明显引入错配，故保持 60 px。
   - 固定焦距和支架，拍 15–30 对棋盘格，跑 `stereo_calibrate.py`，生成 `/etc/vibmon/stereo_calibration.json`。
   - 实测两相机曝光时间差；目前是软件时间戳配对，不是硬件同步。
3. **实拍评测**：仓库里没有 AN5642 原始双目图，本次只用了公开真值数据和合成数据。需要实景双目录制，验证密度、闪烁和测距误差（对照 VL53L1X 或卷尺）。
4. **可选增强**：4 路径 SGM 的质量略好，但要多 2 行路径缓存（约 +9 个 BRAM36），资源允许再上；PL 端做校正重映射（LUT＋行缓存）可省 PS 时间。
