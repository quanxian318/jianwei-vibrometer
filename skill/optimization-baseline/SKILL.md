---
name: optimization-baseline
description: Check whether baseline and candidate measurements share the same conditions before reporting an optimization gain.
---

# 优化对照预检

适用来源。旧版 PL 深度核移除后，LUT 变化对应功能范围变化。

使用方法。对齐板卡、输入、功能范围、时钟、工具版本和指标单位。通过后再使用两份原始报告计算差值。功能范围改变时并列报告版本和功能取舍。 从两份独立记录提取以下字段，分别保存为 JSON 对象。字段为 device, input, scope, clock_mhz, tool, unit。字符串应统一单位和表达后再比较。

在提交包根目录运行：

    python skill/compare_records.py optimization-baseline skill/optimization-baseline/reference.json skill/optimization-baseline/match.json
    python skill/compare_records.py optimization-baseline skill/optimization-baseline/reference.json skill/optimization-baseline/mismatch.json

已验证效果。包内合成正例退出 0。反例退出 1，并报告 scope 不一致。缺字段或格式有误时退出 2。检查通过只表示给定字段一致。

失效条件。即使条件字段相同，测量窗口、误差和统计重复性仍需检查。实现产物本身的哈希可以不同。
