# 工具调用与产物交接

## 软件参考 → RTL → 波形

从本技能目录在 Bash 执行。Windows 可以在 WSL 中进入对应目录。依赖 Python 3 和 Icarus Verilog。

```bash
set -e
python3 scripts/word_vectors.py generate run --count 64 --seed 42
iverilog -g2012 -s tb_axis_add_one -o run/axis.vvp \
  examples/axis_add_one.v examples/tb_axis_add_one.sv
vvp run/axis.vvp +input=run/input.hex +output=run/actual.hex +count=64 +frame=7
python3 scripts/word_vectors.py compare run/golden.hex run/actual.hex
```

预期先输出 PASS protocol，再输出 PASS 64 words, bit exact。试验 1 字帧时，生成和运行均设 count=1，运行加 +frame=1。换题时替换黄金算子和 RTL，保留明确的数据格式、帧边界和长度比较。当前格式每行八位十六进制，对齐 32 位数据。定点滤波应另注明有符号表示、小数位、舍入和饱和。

抓波形时，在 testbench 中加入 $dumpfile("run/axis.vcd") 和 $dumpvars(0, tb_axis_add_one)，重新编译运行后用 gtkwave run/axis.vcd 打开。先对齐 reset 解除、握手和帧尾，定位到第一个不匹配的拍。带厂商 IP、加密模型或时序仿真时，使用工程支持的 XSim 或其他兼容仿真器。纯 RTL 的 XSim 调用可采用 xvlog -sv、xelab <tb> -s <snapshot>、xsim <snapshot> -runall，给测试程序传入工程需要的输入参数。

## Vitis HLS 支路

在对应版本的 Vitis 环境运行，已有组件配置文件 hls.cfg，含实际器件或平台、top、源码、测试平台和时钟。以下以 Vitis 2025.2 的 Unified CLI 为例。

```bash
vitis-run --mode hls --csim --config hls.cfg --work_dir run/hls
v++ -c --mode hls --config hls.cfg --work_dir run/hls
vitis-run --mode hls --cosim --config hls.cfg --work_dir run/hls
```

C 仿真用与主机同源的固定向量。综合查看 achieved II、延迟、估算资源和失败约束。协同仿真验证实际接口。需要导入 Vivado 时选择 Vivado IP 导出格式，再在 IP Integrator 检查接口、时钟和地址。记录配置、日志及实际 IP 包路径。遇到 II 超目标，先区分存储端口冲突、循环依赖与接口背压，再决定拆分存储、展开或流水化。HLS 报告的周期数属于组件，系统耗时需要独立测量。[AMD Vitis CLI](https://docs.amd.com/r/2025.2-English/ug1399-vitis-hls/vitis-v-and-vitis-run-Commands)

## Vivado 批处理与实现证据

复用工程重建 Tcl，先检查它是否记录 part、IP 版本、约束与所有输入文件。下列 build_project.tcl 是目标工程自己的重建入口。第二条从实际 routed checkpoint 采集报告。每次用单独的输出路径，避免前后覆盖。

```bash
vivado -mode batch -source build_project.tcl -log build.log
vivado -mode batch -source scripts/report_checkpoint.tcl -tclargs path/to/routed.dcp run/impl
```

检查日志里的失败阶段和 report_timing_summary 的 setup/hold、未约束路径，再查 DRC 与资源。只在目标器件、时钟约束和测试输入一致时比较优化。保留原始 .rpt、构建版本、SHA、时钟和实验日志。导出板用 .bit 和同一次生成的 .hwh。XSA 可交给需要它的嵌入式构建流程。两种交付物各自记录用途。[AMD Vivado Tcl](https://docs.amd.com/r/2025.1-English/ug835-vivado-tcl-commands/Tcl-Batch-Mode) · [AMD 时序分析](https://docs.amd.com/r/2025.2-English/ug906-vivado-design-analysis/Report-Timing-Summary)

## 上板调用与故障定位

1. 记录 PYNQ 版本、板卡、镜像、IP 路径和 bit/HWH 哈希。用 SSH 或 Jupyter Terminal 在板端执行 python3 -c "import pynq; print(pynq.__version__)"。Windows 可用 Get-FileHash -Algorithm SHA256，Linux 可用 sha256sum。
2. 运行 [验证与移植](verification.md) 的主机生成和 Overlay 预检。现场通过 scp 上传产物和脚本到该板独立目录，再在板端启动。把标准输出与退出码保存到同一实验目录。
3. DMA 失败先查脚本打印的阶段、Linux 内核日志以及 DMA 状态。需要 ILA 时，在实际 DMA 时钟域同时采样两端 TVALID/TREADY/TLAST，触发于最后一拍或连续停顿。用同一向量对照 RTL 波形及板端输出。HDMI 和摄像头业务需另行验证视频流边界与 AXI DMA 契约。
4. 通用性复核可做两种对照。算法轴在同一板上分别运行 identity loopback 与 add-one。平台轴在不同 PYNQ 板上重建同一算子，只替换 PS、约束及 overlay 产物。每次保存契约差异、相同种子、日志和输出哈希。
