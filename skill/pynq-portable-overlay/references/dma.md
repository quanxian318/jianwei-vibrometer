# 简单模式 AXI DMA

典型通路：PS DDR → MM2S → AXI4-Stream 算子 → S2MM → PS DDR。AXI-Lite 配置 DMA/IP。查 BD：DMA 可访问的 DDR 端口、两个时钟域及 reset、AXIS 位宽、TLAST/TKEEP 和长度寄存器。FPGA 端必须在 TVALID && TREADY 时推进，遇背压保持数据和 sideband。

用 pynq.allocate 创建连续缓冲，保持对象存活至两个通道结束。CPU 写入后同步到设备，设备写回后同步到 CPU。PYNQ SimpleDMA channel 在相应 transfer/wait 路径处理缓存；直接寄存器或其他设备路径使用 buffer.sync_to_device / sync_from_device（或该版本的等价接口）。模板显式同步便于核对，重复同步会影响性能，不把模板耗时当纯 PL 耗时。检查 DRE、地址与长度对齐、传输字节上限、dtype 和步长；不要直接把普通 numpy ndarray 交给 DMA。

先准备接收通道，再安排发送；若 HLS 核需软件启动，在能接收输出以后按设计的启动握手执行。TX 完成不等于 RX 完成。使用有界轮询；DMA 未停止时不要释放仍可能被设备写入的内存。模板超时会暂停在当前进程并保留缓冲，供操作者按该板恢复流程确认停机。Notebook 可单独用终端运行脚本。

| 症状 | 首查 | 可区分试验 |
|---|---|---|
| RX 一直 busy | 没有 TLAST、输出拍数不足、TREADY 或复位 | ILA 抓最后一个握手及 TLAST；恒定小向量绕开算子做 loopback |
| DMA 报地址/从设备错误 | DDR 可达、缓冲地址、DRE 对齐、长度上限 | 小而对齐的 buffer；核对 DMA 状态及 Vivado 地址图 |
| 结果像旧数据 | 缓存方向、等待顺序、不同物理缓冲 | 交替输入，RX 填不同哨兵，完成后同步 |
| 错一拍或尾字节 | TLAST/TKEEP 与边界、宽度转换 | 用 1 拍、2 拍及边界尺寸，ILA 对照有效字节 |

参考：[PYNQ DMA](https://pynq.readthedocs.io/en/latest/pynq_libraries/dma.html)、[PYNQ Allocate](https://pynq.readthedocs.io/en/latest/pynq_libraries/allocate.html)、[AMD PG021](https://docs.amd.com/r/en-US/pg021_axi_dma)。
