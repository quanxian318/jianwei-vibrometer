---
name: reproduction-binding
description: Check that a claimed result and its reproduction record identify the same inputs and build artifacts.
---

# 复现产物绑定

适用来源。历史 PL 双目核的 83 次核对和后来 PS 辅助深度属于不同版本。

使用方法。分别从发布记录和实测记录提取源码版本、位流与配置的 SHA256、用例集标识及运行命令。把相对路径和命令规范成双方共同口径后比较。 从两份独立记录提取以下字段，分别保存为 JSON 对象。字段为 revision, bit_sha256, config_sha256, cases, command。字符串应统一单位和表达后再比较。

在提交包根目录运行：

    python skill/compare_records.py reproduction-binding skill/reproduction-binding/reference.json skill/reproduction-binding/match.json
    python skill/compare_records.py reproduction-binding skill/reproduction-binding/reference.json skill/reproduction-binding/mismatch.json

已验证效果。包内合成正例退出 0。反例退出 1，并报告 revision 不一致。缺字段或格式有误时退出 2。检查通过只表示给定字段一致。

失效条件。没有原始日志、使用可变用例标识或哈希从未经核验的清单复制时，需要回到实际文件和执行记录。
