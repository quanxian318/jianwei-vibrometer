# 示例、测量与移植

## 运行步骤

从技能目录执行：

    python scripts/contract.py examples/contract.json --emit board_smoke.py
    python -m py_compile board_smoke.py
    python scripts/contract.py examples/contract.json --overlay-dir /path/to/overlay

最后一条需要实际同名产物。契约校验只查模式、格式和 HWH 实例名，不证明器件匹配或寄存器语义。把生成脚本和同版产物复制到 PYNQ 板，先运行 python board_smoke.py /path/to/overlay 查看 IP；核对后再增加 --download 执行测试。

样例用 [axis_add_one.v](../examples/axis_add_one.v) 实现 uint32 加一回绕。Vivado 中将 PS GP/AXI-Lite 接简单模式 AXI DMA 和输入型 AXI GPIO（示例 RO 探针），DMA 两个存储侧端口接可访问 DDR 的 PS HP 端口。流侧 MM2S→算子→S2MM；32 位、同一时钟、正确复位、只支持整字，若接收侧要求 TKEEP，接全有效 4'b1111。使用 IP Packager 或 BD wrapper 映射两端 TDATA/TVALID/TREADY/TLAST；给 DMA 与 GPIO 分配地址。适配器、PS 预设和 XDC 依板卡产生。本包不内置 bit/HWH，必须用实际目标工程生成。若做纯 loopback，移除算子并把 reference 改为 identity。若做视频变长帧或多输入算子，重新定义收发长度、帧边界、启动协议和黄金参考。

## 证据口径

- 先测 16 字数据，含 0xFFFFFFFF 回绕；再独立进行边界、背压、重复传输和真实载荷测试。若更换数据布局，黄金参考必须复现溢出、舍入和 padding 规则。
- 稳态计时：固定输入与时钟，记录 warmup 次数、样本数和 P50/P95，拆分软件计算、端到端、纯 PL 周期。带宽=实际有效字节/对应阶段用时，注明单向或双向；不可用额定 DDR 带宽代替实测。
- 实现后 source [Vivado Tcl](../scripts/vivado_reports.tcl)，保留原始报告。记录 part、目标周期、WNS/TNS、是否存在未约束路径、LUT/FF/BRAM/DSP 的 used/available，注明综合还是布局布线结果。
- 换板先跑该板自带 overlay，确认 DDR、镜像、PYNQ 版本、CMA 和 FPGA manager；再更新 part、PS preset、约束、时钟/复位并重建。PYNQ-Z2 与适配了 PYNQ 的 Zynq UltraScale+ 板都可采用此工作流；AX7010 原生 Linux 项目只能复用方法，不能直接加载其他板的 bit，也不能假定已有 PYNQ。
- 自制板查 [PYNQ 镜像构建](https://pynq.readthedocs.io/en/latest/pynq_sd_card.html)；按选定 PYNQ release 核对相应工具链，不把 latest 文档的命令当所有版本通用命令。
