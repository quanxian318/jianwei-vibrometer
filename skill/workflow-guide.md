# 工具调用与工作流构建

## 1 工具调用契约

给每一步定义输入、执行方式、输出、通过条件和失败去向。对 FPGA 数据同时记录源码版本、位流或检查点标识、输入样本、参数、工具版本。工具输出是待解析的证据。遇到工具报错，保留标准错误和退出码。

| 工具 | 输入与调用 | 输出与人工核对 |
| --- | --- | --- |
| Python 定点比对 | 从 skill/ 运行下方命令，输入参考与设备 CSV | JSON 和退出码。核对预期用例数与板端执行失败数 |
| Vivado 归档报告 | 有实现检查点时在 Vivado Tcl Console 执行下方命令 | 资源、时序、时钟报告。核对器件、约束和检查点版本 |
| 测量证据复核 | 将原始日志、时间窗口、标定与待审核正文连同对应 SKILL.md 交给模型 | 主张和证据对照表。人工回查原始记录与统计分母 |
| 机电装配复核 | 在 CAD 打开指定版本装配并运行干涉检查。另打开 STL 和实物照片，再套用对应 SKILL.md | 干涉报告、打印件清单及待试装表。人工区分预期螺纹交叠和意外干涉 |

### Python 比对示例

```sh
python golden-compare/compare.py golden-compare/examples/reference.csv golden-compare/examples/pass.csv
python golden-compare/compare.py golden-compare/examples/reference.csv golden-compare/examples/fail.csv
```

在 PowerShell 中用 `$LASTEXITCODE` 读取上一条 Python 命令的退出码，在 POSIX shell 中用 `$?`。应在运行其他命令之前读取。需要留证时将标准输出重定向到 JSON 文件，并保存本次使用的 CSV。

### Vivado 实现结果取证模板

替换检查点路径。输出目录应是本次运行专用目录。该模板需要与检查点兼容的 Vivado 和器件支持。

```tcl
set checkpoint {path/to/routed.dcp}
file mkdir reports
open_checkpoint $checkpoint
report_utilization -file reports/utilization.rpt
report_timing_summary -file reports/timing_summary.rpt
report_clocks -file reports/clocks.rpt
close_design
```

比较前后版本时各自导出一套，确认目标器件、时钟约束和统计层级一致。将 WNS、TNS 和失败端点数与资源数据一起记录。报告生成成功只表示导出完成，仍需读取正文并核对约束覆盖。模板随包提供，本轮未运行 Vivado。

## 2 从任务拆出可复跑的工作流

1. **定义验收条件**。先写出可观察结果，例如同一输入上的有符号定点值逐项一致，且用例完整。
2. **冻结输入**。给每个用例分配稳定键，记录输入、参数和版本。参考计算与设备执行可独立进行，汇合后才开始比对。
3. **执行并分流**。比对退出码 0 时核对覆盖后归档；1 时定位首个数值差异或缺项；2 时修复输入格式或路径。修改后使用同一批输入重跑。
4. **审核结论**。模型只从工具输出和原始记录提取结论。保留无效样本，证据缺失的项目列为待验证。
5. **归档复现**。保存执行命令、输入输出、退出码、版本、验收结论和纠错过程。更换板卡时先替换采集适配及数据格式，再沿用验收流程。

```text
冻结用例与参数
  ├─ 生成参考 CSV ─┐
  └─ 导出设备 CSV ─┴─ 校验格式和覆盖 → 逐项比对 → 人工审核 → 归档
                               ↑             │
                               └── 修复后重跑 ┘
```

## 3 提示词与交接记录模板

> 按输入、执行命令、输出路径、验收条件和失败分支列出每步。先核对工具是否可用，只使用提供的项目证据。确定性比较交给脚本，解释和资料缺口交给模型，版本对应和最终结论由人工审核。对未执行的命令写模板，对缺失的证据写待验证。

每次运行记录：任务 ID、输入和版本、实际命令、工具版本、产物路径、退出码、验证结果、人工复核、待办。将模型提出的修改与实际采用的步骤分开记录，便于保留纠错轨迹。

## 4 合并包的离线验收入口

从提交包根目录执行，证据目录必须是新目录。命令保留每步 stdout、stderr、预期与实际退出码、Python 版本和生成的 PYNQ 脚本。故障注入符合预期时计为通过。

    python skill/run_offline.py run-offline-001
    python skill/delivery_check/verify_manifest.py --root . --manifest manifest.json --require-ascii

脚本跑的是随包样例。Vivado 审计步骤解析归档文本，检查样例能分辨时序通过与约束覆盖缺项。PYNQ 步骤验证契约生成及 Python 语法。

## 5 调用清单

以下命令从包根目录执行，路径改为项目实际文件。测量审核与装配审核按各自入口提供原始证据。

    python skill/sampling_check/check_sampling.py --fs 45 --target 22 --search-max 30
    python skill/implementation_audit/audit_vivado.py --util skill/tests/fixtures/utilization.rpt --timing skill/tests/fixtures/timing.rpt
    python skill/pynq-portable-overlay/scripts/contract.py skill/pynq-portable-overlay/examples/contract.json --emit board_smoke.py
    python skill/pynq-portable-overlay/scripts/word_vectors.py generate vectors --count 64

Vivado 报告导出、Icarus 仿真、板端 Overlay 调用见 [工具链实操](pynq-portable-overlay/references/toolchain.md)。上板前人工核对目标器件、供电、地址及位流和 HWH 版本。

## 6 工作流编排与交接

| 节点 | 输入与调用 | 产物及放行条件 | 失败分支 |
| --- | --- | --- | --- |
| 定义任务 | 总协作入口和分工评审，输入目标、预算与当前工程版本 | 接口契约、验收阈值、版本记录 | 缺资源证据时先采集同版本报告 |
| 功能验证 | 同源输入分两路运行参考实现和 RTL 或设备，再调用定点比对 | 两份输出、差异表和用例覆盖记录 | 格式问题先修导出，差异按键定位，缺项查运行日志 |
| 实现验证 | 导出并审计 Vivado 报告，可与功能验证并行 | 资源、时序、约束覆盖与基线对照 | 命令成功后仍检查覆盖字段，有缺项则保留未签核状态 |
| 实物验收 | 采样检查、测量证据、外设诊断与装配复核 | 日志和实物观察分列，明确无效样本及未验收接口 | 软件通过而实物异常时按接口层、传输层、输出层分流 |
| 移植验证 | 需要 PYNQ 时先检契约、再做 RTL 样例，最后做目标板验证 | 离线、仿真、板级证据分别归档 | 板卡或 DMA 契约不匹配则回到接口定义 |
| 交付 | 固定报告与产物，调用文件清单校验 | 可追溯的文件清单、执行日志和审核结论 | 路径、哈希不符时查来源再重新打包 |

编排时用结构化记录保存每个节点的输入、命令、预期退出码、业务验收条件、输出目录、依赖和失败去向。失败后用同一输入重跑，配置改变时另建运行目录。模型负责分流和解释，脚本负责确定性检查，队员确认上板和实物结论。机器专用工作流可从实物验收节点接入。
