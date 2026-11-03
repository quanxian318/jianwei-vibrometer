# 构建与部署

## PS源码

在Linux/WSL的`src/ax7010/linux/vibmon`执行：

```sh
make -j4 BUILD=build_host build_host/test_drivers build_host/test_vibration_band build_host/test_multipoint build_host/test_render build_host/test_uart16550
build_host/test_drivers
build_host/test_vibration_band
build_host/test_multipoint
build_host/test_render
build_host/test_uart16550
make -j4 BUILD=build_arm CXX=arm-linux-gnueabihf-g++ CC=arm-linux-gnueabihf-gcc AR=arm-linux-gnueabihf-ar CXXFLAGS='-O2 -g -mfpu=neon -mfloat-abi=hard' CFLAGS='-O2 -g -mfpu=neon -mfloat-abi=hard' LDFLAGS='-static -pthread' build_arm/vibmond
```

`config/current_board.example.json`汇总当前链路的配置字段，正式使用前应核对设备路径、外设和标定文件，并保留设备自己的用户设置。

默认源码构建检查布局哈希。历史部署二进制绕过了检查；需要复现该历史行为时，在相同ARM编译配置下显式执行：

```sh
python3 tools/reproduce_legacy_binary.py build_arm/vibmond release/vibmond --acknowledge-unbound-calibration
```

工具校验ELF32符号大小、指令锚点和原始跳转，任何不符都会停止，不会盲目按绝对文件偏移修改。该补丁只复现历史行为，不能证明毫米标定正确；修改ROI或机位后应重新标定。设备快照解压：`gzip -dk release-artifacts/vibmond.gz`。更换编译器或路径后不保证二进制哈希相同。

## PL源码

在`build/vivado`运行`vivado -mode batch -source create_project.tcl`，带`-tclargs build`则继续生成位流。使用Vivado 2025.2，目标xc7z010clg400-1。脚本使用相对路径，包含BD、模块引用RTL、约束和原用户IP目录；厂商IP许可保持原样。
`reports_original_build`来自已生成当前位流的原工程。本次整理仅校验文件依赖和原位流哈希；本次打包未重新运行Vivado，不能把原报告写成移植脚本已重新综合验收。

## 设备运行

`prepare_an5642_runtime.py`加载PL及设置时钟，需要与已预留顶端内存的Linux镜像配合。该操作应在相机/界面停止时执行。当前历史设备配置位于`/root/printer_uart_debug_20261102/board.json`。
本包没有安装开机服务；此前自动启动已回退。UART0运行时恢复已成功，但冷启动与USB触屏仍未完成稳定性验收；tools/start_an5642.sh为历史模板，引用旧位流名，不能直接当作本包的已验证启动入口。UI二进制可以直接以`vibmond -c <板级配置>`运行，Nano串口未准备时将显示离线。

## Nano与APP

Nano、移动APP和PC模拟源码来自MANIFEST所列Git提交，沿用各自构建说明。Nano固件本次未修改、未烧录。SD语音资源保留在`nano_esp32/sd_card/voice`，供后续核对异常文件。

当前报警应用回归：`make BUILD=build_host build_host/vibmond`，随后在本源码目录执行 `python3 tests/test_alarm_consistency_host.py build_host/vibmond`。全量旧test_app_host.py包含重启后认可历史基线的旧断言，与当前“本次运行重新采基线”策略冲突，本次没有宣称该旧全量套件通过。
