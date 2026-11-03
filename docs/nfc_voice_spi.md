# PN532接入后SD或语音异常

## 当前核对结论

主载板网表中，PN532与J603 microSD共享Nano的SCK/MOSI，两个片选独立；MISO经U601两路三态缓冲器隔离。按SN74LVC2G125与74LVC2G125DC-Q100数据手册核对，以下引脚接法一致：

| U601针脚 | 网络 | 用途 |
|---|---|---|
| 1 | SD_CS | /1OE，低有效 |
| 2 | SD_DAT0 | 1A，SD输出 |
| 6 | MISO_BUS | 1Y，接Nano MISO |
| 7 | PN_CS | /2OE，低有效 |
| 5 | PN_MISO | 2A，PN532输出 |
| 3 | MISO_BUS | 2Y，接Nano MISO |
| 8 / 4 | 3V3_D / GND | 缓冲器供电 |

嘉立创最终贴装BOM中，U601由SN74LVC2G125DCUT（C2675813）替换为Nexperia **74LVC2G125DC-Q100H（C548583）**，VSSOP-8、0.5mm间距。其针序和低有效使能与原设计一致；BOM没有显示误换为高有效使能器件。记录见`05_验证资料/Nano固件构建/u601_smt_bom.json`。这仍不代表已核对实物丝印或焊接。

R605、R606均为片选到3V3_D的10k上拉。两个缓冲输出汇合是此设计的预期用法，前提是两片选不会同时有效、实装芯片为相同引脚和低有效使能器件，焊接无短路。图纸一致不能替代实板测量。

已安装Arduino ESP32板包的SPI事务带互斥锁；SD使用Mode 0/MSB first，PN532使用Mode 0/LSB first。此次没有盲目叠加总线锁或修改时钟来宣称修复实板。

用户确认：接上NFC后SD/语音异常，其他功能正常；接线按名称对应、拨码1 OFF/2 ON。暂不方便拆分电源线和信号线，因此故障点尚未实测定位。

## 照片中模块的接线顺序

照片中模块顶端从左到右为SCK、MISO、MOSI、SS、VCC、GND、IRQ、RSTO；与J602针序不同。按信号名称接线：

| 主板J602 | 模块信号 |
|---|---|
| 1 | VCC，5V_AUX |
| 2 | GND |
| 3 | SCK |
| 4 | MOSI |
| 5 | MISO |
| 6 | SS/SSEL |
| 7 | IRQ |
| 8 | 不接；RSTO不接 |

## 0.2.2固件诊断

USB-C或J605控制台使用115200、8N1。以下命令在原有语音、UART和掉电保护基础上增加，不绕过PFAIL保护。

```text
status
spi
nfc off
# 等待约6秒，允许此前失败的SD挂载自动重试
sd
play baseline_done 30
events
nfc on
nfc
```

`nfc off`完成或中止PN532当前事务、尝试关闭RF、将PN_CS置高，然后停止NFC初始化/轮询。它不切断模块供电，也不会强行驱动SD片选。`nfc on`重新初始化PN532；该开关仅本次运行有效，重启恢复默认开启。

- 停止NFC访问后SD/语音恢复：动态SPI/RF活动相关，需要结合片选与供电波形继续定位；尚不能仅据此断定软件原因。
- 停止访问仍异常：优先测量5V_AUX、U601的3V3_D，并检查实装型号、U601焊接、SS/MISO线与模块。没有拆开电源和信号的对照结果，仍无法唯一定位。
- 无SPI事务的空闲时，两片选应均为高；播放时PN_CS应保持高。`spi`的MISO电平只是瞬时取样，不能据其高低判定短路或通信通过。
- 新基线提示需要卡上`/voice/baseline_done.wav`；`sd`列出六条资源是否存在。`AUDIO_DONE`表示软件任务结束，是否有正确声音仍需听音确认。

## 资料依据

- [TI SN74LVC2G125](https://www.ti.com/lit/ds/symlink/sn74lvc2g125.pdf)
- [Nexperia 74LVC2G125-Q100](https://assets.nexperia.com/documents/data-sheet/74LVC2G125_Q100.pdf)
- [Elechouse PN532 V3模块手册](https://www.elechouse.com/elechouse/images/product/PN532_module_V3/PN532_%20Manual_V3.pdf)
- 工程网表：`01_硬件工程/网表/主载板PCB_逐引脚网表.csv`。
