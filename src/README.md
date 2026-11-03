# 设计源码

- `ax7010/linux/vibmon`：PS Linux C++17 主程序、测振核、声学、UI、HTTP、工具和测试。
- `ax7010/pl`：自研 RTL 与测试平台，包含 PDM、LK、双目、载板控制及显示模块。
- `nano_esp32`：Nano 固件、预编译文件、SD 语音和主机协议测试。
- `mobile_app`：Android、iOS、Windows 外壳及共用网页。
- `shared`：PS 与 Nano 共用协议。
- `pc_sim`：PC 模拟环境。

当前板上位流的实际模块集由根目录 `build/vivado/bd/design_1.bd` 定义；`ax7010/pl` 同时保留已完成验证的独立加速模块。
