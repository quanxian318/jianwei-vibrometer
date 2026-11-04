# Arduino Nano ESP32固件

使用Arduino Nano ESP32 ABX00083，板包 **Arduino ESP32 Boards（Arduino）**，FQBN为`arduino:esp32:nano_nora`。程序包含AX链路、PN532 SPI、辅助SD、I2S语音、Wi-Fi/NTP、PFAIL和HTTP手机网关。

- [主程序](vibmon_nano/vibmon_nano.ino) · [引脚定义](vibmon_nano/board_pins.h)。
- [30针硬件对照](../../docs/nano_pins.md) · [SEL和PN532拨码](../../docs/module_switches.md)。
- [已完整编译的固件](bin/firmware_status.md) · [构建验证](../../data/nano_build_validation.json)。
- [SD卡文件](sd_card/) · [十三条语音清单](../../docs/voice_files.csv)。

## 编译与烧录

当前版本为 0.2.3，支持十三个语音资源 ID、NFC、SD、双控制台和 APP 网关。固件已烧录，USB 命令、AX7010 链路、语音播放与 NFC 读卡按整机流程工作。构建输入和产物校验见[构建记录](../../data/nano_build_validation.json)。

1. 主载板断电，取下Nano，单独用USB-C数据线连接电脑。
2. Arduino IDE中选 **Arduino Nano ESP32**，Pin Numbering选 **By Arduino pin (default)**，USB Mode选 **Normal mode (TinyUSB)**，分区保留 **With FAT partition（默认）**。验证板包版本为`2.0.18-20240930.arduino3`。
3. 将本仓库`src/shared/vibproto`文件夹复制到Arduino Sketchbook的`libraries/vibproto`，确保`library.properties`与`src`直接位于其中。无需另装PN532、音频或ArduinoJson库。
4. 打开`vibmon_nano/vibmon_nano.ino`，保留相邻全部.cpp/.h文件，选择Nano端口，验证并上传。没有运行端口时可双击RST进入启动加载模式后重新选择端口。该板使用DFU上传流程，不套用通用ESP32 Dev Module的擦除/引导文件教程。
5. 上传后打开USB串口监视器，115200、换行；自编USB串口客户端需同时置DTR/RTS有效，连接无回复时可保持DTR有效并将RTS关再开。先发送`ping`应返回`pong`，再发送`status`查看版本0.2.3。整机运行时通过载板连接 SD、NFC 与 AX7010 链路。
6. 断开USB，对准D13/VIN、D12/TX丝印插回J604/J606；核对主载板电源后再上电。载板12V仅接Nano VIN，不能插偏到VBUS/3V3。

Windows已有Arduino IDE 1.8.19及板包时，在`06_软件与FPGA`执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File nano_esp32\build_nano.ps1 -ArduinoDir D:\Arduino
```

脚本使用已安装工具链，把源码复制到ASCII临时路径构建，输出到`bin`；只编译，不烧录。默认临时目录`%TEMP%/vibmon_nano_build`会清空重建，不在其中存放其他文件。IDE遇到中文长路径问题时，可将仓库解压到较短的英文路径。

## 语音卡与模块设置

`baseline_done`播放“停机基线已采集，请开始测量”。当前0.2.3预编译固件已包含该资源ID，WAV位于`sd_card/voice/baseline_done.wav`。声音从J603卡读取，未内嵌于应用BIN；需要把新文件复制到卡的`/voice/`并烧录0.2.3。PS 在基线保存成功后调用该 ID，正式测量完成调用`measure_done`。

把`sd_card/voice`整个文件夹复制到J603所插FAT32卡根目录，形成`/voice/measure_done.wav`等十三个路径。这是Nano辅助卡，与AX7010 Linux启动卡分开。

十三条文件为选定的“清甜灵动”女声：百度度清影，per=4196，16kHz、16bit、单声道PCM WAV。测量完成使用已选试听；预警、报警和异常用同一声线的中性语气。文案、时长、合成参数和SHA256见[合成记录](sd_card/voice_manifest.json)。播放无需联网，也无需百度API密钥。十三条 WAV 已写入实物语音卡并完成读回校验，基线完成、测量完成和报警语音随操作事件播放。

新增七条资源为`welcome`、`baseline_start`、`baseline_failed`、`measure_start`、`measure_failed`、`calibration_done`、`print_sent`。它们已合成并列入0.2.3白名单，可通过`play welcome 30`等命令手动播放；由语音接口按事件或控制台命令调用。采集前提示应在采集开始前播放完，不与PDM声学窗口重叠。十三条资源统一保存在卡的 /voice 目录。

PN532 V3走SPI：拨码1 OFF、2 ON，按本块开关ON标记设置并重新上电。J602-8为NC，不接模块RSTO。J601接无源喇叭，SPK_P/SPK_N两脚均不是GND。MP34DT01不经Nano，由AX的PL/PS处理。

## 通信与检查

UART默认115200、8N1、RTS/CTS，与PS源码默认配置一致；NVS历史设置可能覆盖默认值，更改baud后按提示重启。Nano/AX串口、SPI和I2S已在载板连接，不另加跳线。

默认热点`VIBMON-xxxx`，密码`vibmon2026`，地址`192.168.4.1`，HTTP 80、发现UDP 48300。测量状态需要AX/PS发布，设备状态由 AX7010 经网关实时发布。

USB-C与J605控制台均可用（CR、LF或CRLF结束命令）：

```text
help
ping
status
events
sd
spi
nfc
nfc off
play baseline_done 30
nfc on
play measure_done 20
stop
wifi <ssid> [password]
ap on
ap off
baud 115200
flow on
reboot
```

`nfc off`停止本次运行的NFC初始化与轮询、保持PN_CS高，不切断模块电源；`nfc on`重新初始化，重启默认开启。关闭后等待约6秒再用`sd`检查挂载并播放语音。`spi`仅报告瞬时片选/MISO电平与服务状态。NFC 与 SD 共用 SPI 总线，操作方法见[共用SPI排查](../../docs/nfc_voice_spi.md)。

### J605接线与调试

J605为UART0（`Serial0`），固定 **115200、8N1、无流控、3.3V逻辑**；与AX7010的UART1分别使用不同引脚。`baud`/`flow`命令只修改AX链路，J605始终115200。

| J605电气针号 | Nano信号 | USB-TTL接线 |
|---|---|---|
| 1 | GND | GND |
| 2 | D1/TX，GPIO43 | RXD |
| 3 | D0/RX，GPIO44 | TXD |

J605没有供电脚，不接适配器VCC。Nano脱离主载板时可单独USB供电，按D1/TX、D0/RX和GND连接；插在载板上时使用已核对的载板供电。连接后发`ping`、`help`、`status`，也可使用`sd`、`nfc`及`play measure_done 20`检查实际接入的外设。PFAIL或掉电准备状态下仍拒绝播放，不绕过保护。

两个入口独立组装命令，回复与日志同时输出到USB和J605；USB未打开也能独立使用J605。每路16KiB发送队列按可用FIFO容量分段发送，每轮最多发送128字节，不因USB终端停读而等待。队列满时丢弃新整条输出；超过160字节的输入整行拒绝，避免执行截断命令。控制台可影响设备运行，AX二进制协议不接到J605。


### 控制台主机回归

使用已安装的WSL g++，在本目录运行（只模拟串口和FreeRTOS互斥锁，不代表实物串口验收）：

```sh
g++ -std=c++11 -Wall -Wextra -Werror -pthread -Itest/console_stubs -Ivibmon_nano test/test_console_io.cpp vibmon_nano/console_io.cpp -o /tmp/vibmon_console_io_test
/tmp/vibmon_console_io_test
```
