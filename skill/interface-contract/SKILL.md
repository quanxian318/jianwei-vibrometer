---
name: interface-contract
description: Audit a hardware/software interface handoff before combining a bitstream, driver, and register map.
---

# 接口版本交接

适用来源。项目的 HP1 集成版与通用 HP2 配置曾使用不同的地址和接线。

使用方法。核对双方共同的构建标识、接口、基地址、数据宽度和中断标识。对寄存器语义和访问权限保留人工复核。 从两份独立记录提取以下字段，分别保存为 JSON 对象。字段为 build, bus, base, width, irq。字符串应统一单位和表达后再比较。

在提交包根目录运行：

    python skill/compare_records.py interface-contract skill/interface-contract/reference.json skill/interface-contract/match.json
    python skill/compare_records.py interface-contract skill/interface-contract/reference.json skill/interface-contract/mismatch.json

已验证效果。包内合成正例退出 0。反例退出 1，并报告 bus 不一致。缺字段或格式有误时退出 2。检查通过只表示给定字段一致。

失效条件。共同标识缺失、双方源于同一错误表格或地址在运行时动态分配时，先补权威地址分配及运行证据。
