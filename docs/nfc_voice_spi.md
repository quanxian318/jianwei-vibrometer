# NFC 与 SD 语音共用 SPI

PN532 与 microSD 使用同一组 SPI 时钟和数据线，通过独立片选轮流访问。NFC 读卡与 SD 语音播放已完成整机联调。

PN532 V3 使用 SPI 模式，拨码 1 OFF、2 ON，设置后重新上电。NFC 接 J602，语音卡接 J603。访问 NFC 时保持 SD 片选为高，访问 SD 时保持 PN_CS 为高。

语音资源为 /voice 目录中的十三条 WAV，格式为 16 kHz、16 位、单声道 PCM。读取 SD 文件后通过 I²S 输出，NFC 轮询按事务释放 SPI。

## 控制台

- nfc on、nfc off 控制读卡轮询。
- spi 显示片选、MISO 和服务状态。
- sd 显示卡状态。
- play baseline_done 30 播放基线完成提示。

卡片进入时产生 NFC 事件，同一卡持续停留时去重。基线完成、测量完成与报警由 PS 提交对应语音任务，完成后通过事件队列返回结果。

[Nano 固件](../src/nano_esp32/README.md)和[接口协议](ps_nano_protocol.md)给出命令与事件格式。
