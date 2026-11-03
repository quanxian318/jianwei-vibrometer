# 见微视觉测振仪 AMD 技能包

本目录包含十三项技能或工作流入口。优先从协作总流程选择阶段，再调用对应专项。保留原始脚本和相对目录，便于复跑。工具命令、产物交接和失败分支见 workflow-guide.md。

| 阶段 | 入口 | 证据与边界 |
| --- | --- | --- |
| 工作流编排 | [FPGA 大模型协作](fpga-llm-collaboration/SKILL.md) · [工具速查](workflow-guide.md) | 总入口负责分流，工具速查负责命令和归档，历史案例见 report |
| 架构评审 | [PL 与 PS 分工](../docs/nano_build.md) | 有项目架构调整案例，功能不同的版本须分列 |
| 定点验证 | [整数黄金参考比对](../docs/nano_build.md) | CSV 对比已复跑，历史板端 83 次核对对应旧版双目 PL 计算链 |
| 实现检查 | [Vivado 报告审计](../docs/nano_build.md) | 英文报告样例可复跑，区分已约束路径与约束覆盖 |
| 采样复核 | [采样脚本](../docs/nano_build.md) · [测量证据](measurement-evidence/SKILL.md) | 前者核查频率必要条件，后者复核报告、标定和无效样本 |
| 外设诊断 | [外设分层诊断](../docs/nano_build.md) | 区分接口、传输和实物输出，须按实际板卡复查 |
| 装配复核 | [机电装配](mechanical-fit/SKILL.md) | CAD 和照片证据分别记录，实物试装独立验收 |
| 交付验证 | [文件与 SHA256](../docs/nano_build.md) | 检查文件一致性与命名，功能需单独验收 |
| 可选移植 | [PYNQ Overlay](pynq-portable-overlay/SKILL.md) | 原包记录离线预检和 RTL 仿真，PYNQ 上板仍待验证 |

定点比对用于通用键值 CSV。PYNQ 示例自带的字流比对包含 DMA 帧长和传输契约，保留两个入口。Vivado 导出模板用于产生报告，报告审计脚本用于解读，两者可串联。

从包根目录执行 `python skill/tests/test_skills.py`。定点对比例子和 PYNQ 离线命令分别见各入口及 [PYNQ 原验证记录](../report/validation.md)。协作过程与原验证说明见 [协作报告](../report/agent_collaboration_and_skills.md)、[交互选摘](../report/interaction_records.md) 和 [归并记录](../report/consolidation.md)。

从包根目录执行 python skill/run_offline.py run-offline-001 可复跑整组主机样例。工具命令、节点交接和失败分支见[工具调用与工作流构建](workflow-guide.md)。

## 新增的三项证据核查

1. [接口版本交接](interface-contract/SKILL.md)放在烧录或联调前。
2. [优化对照预检](optimization-baseline/SKILL.md)放在性能与资源数据汇总前。
3. [复现产物绑定](reproduction-binding/SKILL.md)放在形成技术报告结论前，再执行交付哈希校验。

三项可独立使用，命令及合成样例见各入口。验证结果见 [新增技能记录](../report/skill_extension_validation.json)。

[评审复跑案例](../report/review-cases.md)覆盖 Vivado 报告与优化范围对照。一键入口也执行新增三项的正例和反例。
