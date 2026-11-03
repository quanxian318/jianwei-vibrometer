# 评审复跑案例

在提交包根目录运行 `python skill/run_offline.py run-review-001`。逐步输出在该目录，汇总为 summary.json。预期拒绝属于通过的检查。

## Vivado 实现报告审计

    python skill/implementation_audit/audit_vivado.py --util skill/tests/fixtures/utilization.rpt --timing skill/tests/fixtures/timing.rpt

输入是原技能包归档的 Vivado 2025.2 报告样例。结果含 LUT 8945、FF 14420、BRAM 30.5、Slice 4017，WNS +0.756 ns、WHS +0.028 ns。样例列出 592 个无时钟项、1132 个未约束内部端点。可复跑结论是已约束路径通过，且报告列出的约束覆盖检查有缺项。该归档与早期 +0.004 ns 弹性缓冲实现记录属于不同版本。

## 功能范围不同的资源对照

    python skill/compare_records.py optimization-baseline report/review-cases/baseline.json report/review-cases/candidate.json

项目协作报告记录了辅助深度从独立 PL 核调整到 PS 的过程，LUT 从 13655 变为 8574。两份 JSON 将这一功能差异编码为 scope；其余字段是受控演示值，用来单独验证范围变化会触发拒绝。预期退出 1，输出 scope 差异。报告可以并列列出架构与资源变化。同功能优化率需要独立的同条件基线。

PYNQ 的离线预检和 RTL 仿真见原验证记录。目标板移植仍需单独执行。
