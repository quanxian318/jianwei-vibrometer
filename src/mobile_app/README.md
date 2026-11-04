# 见微测振APP

[使用、安装、三平台构建及API说明](../../docs/app_guide.md)。

- `web`：三平台共用界面与HTTP客户端。
- `android`、`ios`、`windows`：各自原生外壳。
- `dist`：当前安装包；iOS包需自行签名安装。
- `tools`：构建与PCB纹理生成脚本。

[三侧接口说明](../../docs/interface_review.md)说明 AX7010、Nano 网关与 APP 的数据和命令交互。默认同一局域网直接连接，也可启用配对。APP 版本为 1.0.5，Nano 固件为 0.2.3。PCB 板图随 APP 源码提供，制造文件位于 hardware/pcb。
