---
name: golden-compare
description: Compare integer golden-reference and accelerator outputs aligned by case and element, and diagnose missing or mismatched results.
---

先确认两侧使用同一输入、配置、位宽、有符号解释、量化和舍入约定。导出双方有唯一键的 CSV，按 [README](../../docs/nano_build.md) 运行比对。保留命令、退出码和原始输入。若样本缺失或格式错误，先修复采集或导出；若数值不一致，从首个差异所在阶段追查边界、位宽、溢出或重排。独立记录设备执行失败，避免只导出成功结果后报告通过率。
