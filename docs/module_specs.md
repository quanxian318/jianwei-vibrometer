# 已选模块：采购链接与规格

2026-10-27按用户本轮提供的12个链接登记。其中短标题“24”“4.8”“95”已由用户依次确认是GY-291 ADXL345、12V补光灯、热敏打印机。以下保留原始分享地址；本次浏览工具无法读取淘宝商品页，不据链接推断已选SKU、实时库存或商家线色。

[逐脚接线总表](wiring.md) · [资料与缺口](nano_build.md)

| 编号 | 配件/已知选择 | 淘宝链接 | 主板连接 | 规格与待确认 |
|---|---|---|---|---|
| 01 | MP34DT01 PDM麦克风 | [打开商品](https://c.tb.cn/h.j3HadfARJGUp9LN?tk=SjC6TwmhEcr) | J706 | 接3.3V、GND、CLK、DAT；用户已确认型号MP34DT01；SEL/L/R按单麦方案接GND，先核对模块内部固定连接；模块电路待实物核对，现有MP34DT01离线手册适用该芯片型号。 |
| 02 | 12V/650nm十字线红光投影 | [打开商品](https://c.tb.cn/h.jWR2SRAIC5UCfys?tk=1EehTwmSk9m) | J304 / LED2 | 用户既定12V、650nm十字线；商品有3–12V等选项，保存订单规格及额定电流。 |
| 03 | 普莱特三色声光警示灯 | [打开商品](https://c.tb.cn/h.jWR2oLPQHkXsLb6?tk=z7QBTwm7nYm) | J302-1…5，经SW302 | 需要12V、公共正极、红黄绿及蜂鸣器四路独立负端；实际线色、是否含蜂鸣器、公共极性待该款接线图。 |
| 04 | GY-291 ADXL345 | [打开商品](https://c.tb.cn/h.jWRWL0xm218O5gE?tk=2ASYTwmRxEn) | J703 | 用户本轮确认标题24对应本模块；选可用4线SPI的版本，核对SDO没有硬接GND及3.3V供电路径。 |
| 05 | GY-SHT40温湿度模块 | [打开商品](https://c.tb.cn/h.jVhTUHBVcwJIPHy?tk=gMrUTwm8yFL) | J704 | 按3.3V供电；按VIN/GND/SCL/SDA丝印转接，上拉只能到3.3V，板色不作为有无上拉证据。 |
| 06 | 7423腔体喇叭，8Ω | [打开商品](https://c.tb.cn/h.jWReIdtof9VhgUI?tk=3lzFTwm8jmz) | J601 | 本设计选8Ω、额定3W两线无源款；商品含2W/3W多选，实际订单功率和尺寸待确认。无需外购功放模块。 |
| 07 | 12mm IP67带灯自锁按钮 | [打开商品](https://c.tb.cn/h.jVh6yvSevy9arE9?tk=OOPmTwmjivy) | SW101/SW301/SW302/SW303 | 触点与灯脚分别核对。总电源/声光/风扇灯按12V；SW301现有指示接法需5V灯。不能把同一12V灯规格用于全部四个接法。 |
| 08 | 12V两线散热风扇 | [打开商品](https://c.tb.cn/h.jd6T58zo5ueGq39?tk=z1WNTwm9RPo) | J302-1经SW303到正端，J302-6到负端 | 确认两线12V款、外形、运行及启动电流；不能按四线电脑PWM风扇接法使用。 |
| 09 | 12V补光灯 | [打开商品](https://c.tb.cn/h.j3Hc0lbBFV7LD64?tk=9ttJTwm9lwa) | J301 / LED1 | 用户本轮确认标题4.8对应补光灯；需要自带驱动/限流的12V成品，功率及PWM兼容性待订单规格。 |
| 10 | VL53L1X ToF模块 | [打开商品](https://c.tb.cn/h.jd6h0yNZFLDn2os?tk=4pKETwmkKiT) | J701 | 选择VL53L1X、3.3V供电及逻辑兼容款；GPIO1/XSHUT是否引出按实物确认。 |
| 11 | PN532 NFC RFID V3模块 | [打开商品](https://c.tb.cn/h.jWRfvcPt9C6uS2t?tk=ZmNQTwmlcgH) | J602 | 5V模块电源、3.3V SPI逻辑；断电设置SPI：拨码1 OFF、2 ON（按ON标记）；重新上电，J602-8为NC。 |
| 12 | EM5820H热敏打印机，12V版 | [打开商品](https://c.tb.cn/h.jVhiT690g65idYq?tk=uBp1TwmPfqU) | J401 | 用户本轮确认标题95对应打印机，前文确认12V版；TTL版本、MX1.25端实际针号、线色、CTS及协议仍需批次说明。 |

未在本轮提供购买链接的现有部件：AX7010、AN5642、Arduino Nano ESP32 ABX00083、微雪7inch HDMI LCD(H)、LSM6DSV、12V适配器、40P IDC线、前面板10P线、模块转接线、CR1220和SD卡。沿用既定型号，不编造商家链接；原厂与手册入口见资料覆盖页。
