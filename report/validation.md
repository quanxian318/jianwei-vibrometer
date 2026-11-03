# 技能包验证记录

| 检查 | 方法 | 结果 |
|---|---|---|
| 技能结构 | Codex skill-creator quick_validate.py 分别检查两个技能入口 | 通过 |
| 契约与生成器 | 读取样例 JSON，生成主机脚本并由 Python 编译检查 | 通过 |
| 离线预检 | 最小模拟 HWH 提供两个期望实例名 | 通过；模拟 bit 文件只是占位输入，不是位流 |
| 故障注入 | 不对齐偏移、缺少 bit、缺失 IP、损坏 XML | 均按预期退出并返回失败 |
| 提交树 | 检查文件名、相对链接、压缩包完整性 | 通过 |
| PYNQ 实板 | Overlay 下载、寄存器读取、DMA 缓存与性能、Vivado 报告 | 本轮未执行；需要兼容板和该板生成的 bit/HWH。示例 RTL 的仿真结果见下表。 |

复现离线正向测试：在技能目录执行 `python scripts/contract.py examples/contract.json --emit board_smoke.py`，然后 `python -m py_compile board_smoke.py`。负向测试可将 JSON 的 read_only_offset 改成 3，或指定一个没有位流的 --overlay-dir，预期退出码 2。

“见微”既有 Git 证据与本包验证分列，见 [项目案例](project-case.md)。

## 扩充版的复跑结果

| 检查 | 实际结果 |
|---|---|
| WSL + Icarus Verilog + Python 黄金参考 | 1、64、1024 个 uint32 数据字全部逐位相等，帧长分别为 1、7、16。覆盖周期性背压、帧尾和 32 位回绕。 |
| 截短输出 | 将 1024 字输出截为 2 字，比较器报告 1022 个不匹配，退出码为 2。 |
| Vitis HLS / Vivado / PYNQ | 本轮提供工具调用和验收步骤。HLS 综合、Vivado 布局布线和 PYNQ 上板尚待运行。 |

复跑命令见 [工具调用与产物交接](../skill/pynq-portable-overlay/references/toolchain.md)。

## skill-creator 复修

两个 SKILL.md 的场景入口及相对引用均已核对。两套技能的 Codex UI 元数据由官方脚本生成，quick_validate.py 均通过。已有 RTL 仿真结果对应本包中相同的示例源码。
