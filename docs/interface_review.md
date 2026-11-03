# ESP32、AX7010、APP三侧对接审查

核对版本：GitHub main `7a2bb767785b2bf3d1d557239eceefc233d50ba3`，2026-10-27。协议与APP问题使用独立源码快照核对，未改程序、RTL、板端或冻结标签；Nano整包构建状态更新于2026-10-28，见[构建验证](nano_build.md)。

结论：源码主协议与主要控制命令对应，但目前不能称为三侧已经完整实机联通。已复现两处APP行为问题；部署版本和载板PL集成还有缺口。

## 能对应的部分

| 链路 | 对应关系 | 本次证据 |
|---|---|---|
| APP直连AX7010 | HTTP 8080 `/api/v1`，UDP 48300发现；info/live/settings/points/records/command、预览和文件下载 | 当前PS程序构建成功，105项主机集成检查通过，包含HTTP路由、测量和记录 |
| APP经ESP32到AX7010 | ESP32 HTTP 80；APP命令转APP_COMMAND事件，AX执行后以0x62返回cid结果；APP轮询command/cid | 真实ESP32网关核心主机测试通过；PS与伪终端模拟Nano之间发布/执行/回传通过 |
| AX到ESP32 | 共用shared/vibproto；协议版本字节1，0x60发布、0x62命令结果，载荷上限1024字节；session/事件ACK机制对应 | 主机集成检查含会话、主题发布、命令白名单和帧大小 |
| 串口配置 | 两端默认115200、8N1、RTS/CTS开启；Nano D2 TX/D3 RX/D4 RTS/D5 CTS与既定载板针表对应 | 静态核对；ESP32 NVS保存过的参数可能覆盖默认值，实物流控和波形未测 |

网关模式按现有实现只传紧凑状态、少量最近记录及最新结果抽稀曲线，不承载相机画面、完整历史曲线和CSV文件；APP相应能力开关已区分。需要这些内容时使用AX7010局域网直连。

## 已复现的问题

1. **声学结果在APP中隐藏，且字段/结论不对应。** `mobile_app/web/js/page_records.js:146`检查`ac.enabled`并读取`ac.analysis.frequency_hz`，但PS `src/app/storage.cpp`保存的是`acoustic.available/valid`和`acoustic.visual.acoustic_hz/relation`。实际结构传入APP详情渲染器后，“声学参考”行不出现；此外当前渲染把所有valid结果写成“一致”，没有区分agree/harmonic/differ等结论。复现数据在`app_contract_probes.json`。

2. **快速修改不同设置丢失前一项。** `mobile_app/web/js/page_settings.js:58–60`对所有设置共用一个350ms定时器，下一次修改会取消上一次待发请求。实际运行该模块，间隔30ms先改voice=true再改buzzer=false，最终只发送buzzer=false。PS没有收到voice，因此界面操作与设备状态不一致。应合并待发字段或按字段独立处理，修复后补回归。

## 部署与硬件缺口

- Nano 0.2.0已于2026-10-28使用真实板包完整编译并更新bin，包含手机网关和当前保护实现，见[固件状态](../src/nano_esp32/bin/firmware_status.md)。烧录与实机联调尚未验收，主机测试及编译通过不代表实体链路已经接通。
- 此前板端冻结提交`a01fed8`不含`app_api.cpp/app_remote.cpp`，当前main才新增。若板子仍用该冻结程序，APP无法凭最新源码直接使用这套HTTP/网关接口；本轮未读取或改动板子，不能声称当前已部署最新程序。
- 当前`team_design.tcl`只组合双目与PDM相关脚本；载板控制、Nano UART等在独立`add_carrier.tcl`。必须完成与当前相机设计的PL/设备树/串口节点集成，不能只烧新Nano固件就宣称实体串口链路已接通。
- MP34DT01走PL/PS，不经ESP32；这与三侧职责一致。触屏、编码器、打印机等实体联调不在本次主机测试范围。

## 验证文件

- `ps_build.log`：当前PS源码构建日志。
- `ps_app_integration.log`：105项通过（含HTTP与模拟Nano网关）。
- `gateway_core_test`：当前ESP32 app_gateway.cpp的主机构建测试程序，执行输出all checks passed。
- `app_contract_probes.cjs`及JSON：实际APP模块的两项复现。

本次问题尚未修复，维持用户要求的代码冻结。后续应先修这两项APP问题，再把PS程序、Nano固件与载板PL作为对应版本完成构建和实体联调。
