# Overlay 与寄存器

- 从同一次实现导出同名 .bit / .hwh，记录哈希、目标 part、时钟与设计 revision。有需要时提供该镜像支持的设备树叠加文件。不同板即使器件相同，也不能沿用引脚和 PS 配置。
- Python 先执行 Overlay(path, download=False) 检查 ip_dict/hierarchy_dict；这能发现元数据问题，不会证明实物兼容。确认设备与产物后再执行 ol.download()。已加载 Overlay 改变时重新绑定 IP 对象。
- 地址来源以生成的 HWH/Address Editor 加 IP 的寄存器定义为准。打印 ip.register_map（有寄存器元数据时）对照字段和 access；没有元数据时才按确认的字节偏移使用 ip.read(offset)/ip.write(offset, value)。不能把寄存器序号当字节偏移，也不要对 W1C 或读清状态寄存器盲目做读改写。
- 初次 MMIO 验收只读版本或明确无副作用的寄存器，再对可恢复的 scratch 寄存器读写。HLS AP_START/AP_DONE 等启动/完成机制按该 IP 生成的寄存器头文件确认，不能把示例偏移复制给不同 IP。
- 参考：[PYNQ Overlay 设计](https://pynq.readthedocs.io/en/latest/overlay_design_methodology/overlay_design.html)、[Overlay API](https://pynq.readthedocs.io/en/latest/pynq_package/pynq.overlay.html)。
