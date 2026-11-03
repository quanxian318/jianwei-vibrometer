# pc_sim：电脑摄像头仿真

在 Windows 电脑上用自带摄像头运行视觉处理算法，并与 PS 的 C++ 实现、PL 的 RTL 逐位比对。只用电脑上已有的 Python 3 库：numpy、Pillow、PyAV（DirectShow 取帧）、tkinter；RTL 仿真和 C++ 编译在 WSL 里进行（g++、make、Icarus Verilog）。

| 文件 | 用途 |
|---|---|
| `vision_model.py` | 位精确的整数模型（降采样、上采样、时间带通、放大、能量），与 `src/measure/vision.cpp`、`ax7010/pl/vision/rtl` 同一规格 |
| `camera.py` | 摄像头取帧，输出 8 位灰度（YUYV 的 Y，与 OV5640 链路相同），带设备时间戳 |
| `webcam_lab.py` | 实时实验台：运动放大、振动能量热力、主频分布、原图；左图点击放置测点并用与 PS 相同的 NCC 跟踪画位移和频谱；右图点击查看该像素亮度时序和亮度谱；“录制 90 帧”保存片段 |
| `check_bitexact.py` | Python 模型与 C++ `vision_ref` 逐位比对；`--rtl` 再加 RTL |
| `run_rtl.py` | 在 WSL 用 Icarus Verilog 跑 `tb_vp_reduce` / `tb_vp_core`，与模型逐位比对 |
| `stereo_benchmark.py` | 双目匹配真值评测（Middlebury 场景，含曝光差与垂直错位扰动），对比队友 BM、PL 双目核、PS 后处理；需 `stereo_eval` |
| `stereo_calibrate.py` | AN5642 双目棋盘格标定（OpenCV），输出板端 `stereo_calibration.json`；`--self-test` 用合成机位自检并导出 OpenCV 校正映射供交叉核对 |

```bash
python webcam_lab.py
```

```bash
python check_bitexact.py --synthetic
```

```bash
python run_rtl.py core --synthetic
```

不加 `--synthetic` 时用摄像头实拍帧。比对结果和录制片段写到 `%TEMP%\vibmon_pcsim`；实拍片段含摄像头画面，用完可删除。`webcam_lab.py --selftest 12` 会自动运行 12 秒、放置测点和探针、保存各面板图片后退出，用于检查环境。

摄像头的实际帧率受光线影响（室内偏暗时约 10 fps），时间带通的系数按实测帧率计算。电脑摄像头的自动曝光、卷帘和压缩处理与 AN5642 不同，这里验证的是算法和实现一致性，不代表整机测量指标。
