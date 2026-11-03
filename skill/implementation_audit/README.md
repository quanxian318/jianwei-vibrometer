# Vivado实现报告核查

适用场景：检查汇报页资源数字、判断正WNS能否支持“时序全部收敛”的表述。
输入：同一实现版本的英文utilization.rpt与timing.rpt；人员先核对器件、工程和时间。脚本不代替版本确认。
使用：`python audit_vivado.py --util <file> --timing <file>`。
输出：LUT、FF、Slice、BRAM、DSP及WNS/WHS，分别给出已约束路径是否通过、列出的约束覆盖检查是否存在告警。
来源：当前工程LUT8945/17600、FF14420/35200、BRAM30.5/60、Slice4017/4400；WNS+0.756、WHS+0.028，但no_clock592、unconstrained_internal_endpoints1132。
已验证：原报告数值与以上检查项可正确提取，测试确认不会将正余量等同于完整约束覆盖。
失效条件：非英文报告、表格列变化、不同阶段的报告混用、检查项缺失。脚本遇到缺项报错；它不检查全部CDC、例外路径与板级时序，也不证明物理时钟不存在。
