# 定点输出核对

适用于能导出整数参考值和设备值的 RTL、PL、软件仿真或加速器。使用 Python 3.8 及以上，标准库即可。两份 UTF-8 CSV 的表头均为 case_id,index,value。case_id 标识输入用例，index 为非负元素序号，value 为已按双方共同格式解释的十进制整数。每个键必须唯一。多输出通道可分文件或把通道纳入 case_id。

从 skill/ 运行以下命令。

    python golden-compare/compare.py golden-compare/examples/reference.csv golden-compare/examples/pass.csv
    python golden-compare/compare.py golden-compare/examples/reference.csv golden-compare/examples/fail.csv

第一条应返回 0。第二条应返回 1，且指出 case_a 的 index 1 数值差异及 case_b 的 index 0 缺项。退出码 2 表示文件或格式错误。输出为 JSON。条目顺序可不同。

来源为见微双目计算冻结记录。诊断配置每四次启用核对，共 83 次、零差异、零 PL 失败。这是项目既有运行统计。示例 CSV 为合成数据，用来独立验证本脚本的通过、差异与缺项行为。

失效条件包括参考模型错误、编码约定不同、键关联到错误帧、双方一起漏报整组样本以及浮点近似任务。须由外部清单核对预期用例覆盖，另查 PL 执行日志。通过只证明导出数据的逐项一致性。
