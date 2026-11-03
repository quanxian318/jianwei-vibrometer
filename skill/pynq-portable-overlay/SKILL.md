---
name: pynq-portable-overlay
description: 开发、移植和排查 PYNQ Overlay。用于核对 bit/HWH、寄存器、简单模式 AXI DMA，或串联 RTL/HLS、Vivado 和板端验证。
---

先查目标板、器件、PYNQ 镜像与版本，以及 Vivado/Vitis 版本。只有普通 Zynq Linux 时，先确认 PYNQ 镜像适配。按用户任务选择入口：

| 当前任务 | 加载 | 首个结果 |
|---|---|---|
| 有 bit/HWH，需检查 Overlay 或 MMIO | [Overlay 与寄存器](references/overlay.md) | 不下载位流的预检，确认 IP 实例、偏移和副作用 |
| 有 DMA 错误、短包或超时 | [DMA 手册](references/dma.md) | 同长度小向量和可区分缓存、TLAST、地址问题的实验 |
| 从源码复跑或采集实现报告 | [工具调用](references/toolchain.md) | 可逐字比对的 RTL 输出，或同一次实现生成的报告 |
| 换板、上板或对照性能 | [验证与移植](references/verification.md) | 兼容性清单和分层验收结果 |

接口取自实际 HWH、BD 和寄存器定义。仅在单输入、单输出、等长 uint32 的简单模式 DMA 契约匹配时使用 [脚本生成器](scripts/contract.py)。其他数据布局、传输模式或启动协议需按硬件定义适配。换板须重建同源 bit/HWH，核对镜像和 PS 配置。

从小向量开始，再扩到实际负载。保留输入、黄金输出、实际长度和首个错点。分开报告离线预检、仿真、综合实现与板测。位流下载前核对目标和正在运行的服务。
