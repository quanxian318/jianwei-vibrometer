# Nano ESP32两排针逐针对照

元件面俯视、USB-C朝上：左J604、右J606；两排从USB端1到天线端15。优先使用D/A符号，GPIO列用于底层ESP-IDF和排查映射。

| 连接器 | 针号 | 位置 | Nano丝印 | ESP32-S3 GPIO | 网络或状态 | 连接说明 |
|---|---|---|---|---|---|---|
| J604 | 1 | 左排，USB端向天线端 | D13/SCK | 48 | NANO_SCLK | R604源端，SD/PN532共用SPI时钟 |
| J604 | 2 | 左排，USB端向天线端 | 3V3 | — | 不接 | 不与载板3V3_D或AX_3V3相连 |
| J604 | 3 | 左排，USB端向天线端 | B0/BOOT0 | 46 | 不接 | 启动相关脚 |
| J604 | 4 | 左排，USB端向天线端 | A0 | 1 | NANO_A0_IRQ | 经R612接PN532 IRQ |
| J604 | 5 | 左排，USB端向天线端 | A1 | 2 | SD_CD | SD卡座插入检测 |
| J604 | 6 | 左排，USB端向天线端 | A2 | 3 | 不接 | 启动相关脚，不加外部偏置 |
| J604 | 7 | 左排，USB端向天线端 | A3 | 4 | PN_CS | PN532片选及U601 /2OE |
| J604 | 8 | 左排，USB端向天线端 | A4 | 11 | 不接 | 保留 |
| J604 | 9 | 左排，USB端向天线端 | A5 | 12 | 不接 | 保留 |
| J604 | 10 | 左排，USB端向天线端 | A6 | 13 | NANO_A6_PFAIL | R105之后，读取PFAIL_N；是数字输入 |
| J604 | 11 | 左排，USB端向天线端 | A7 | 14 | AMP_EN | 经R617接MAX98357A SD_MODE |
| J604 | 12 | 左排，USB端向天线端 | VBUS | — | 不接 | USB 5V节点，不接12V或载板5V |
| J604 | 13 | 左排，USB端向天线端 | B1/BOOT1 | 0 | 不接 | 启动相关脚 |
| J604 | 14 | 左排，USB端向天线端 | GND | — | GND | 载板共地 |
| J604 | 15 | 左排，USB端向天线端 | VIN | — | NANO_VIN | 12V_MAIN经F201供电 |
| J606 | 1 | 右排，USB端向天线端 | D12/MISO | 47 | NANO_MISO | 经R603接U601输出MISO_BUS |
| J606 | 2 | 右排，USB端向天线端 | D11/MOSI | 38 | NANO_MOSI | R602源端，SD/PN532共用MOSI |
| J606 | 3 | 右排，USB端向天线端 | D10/CS | 21 | SD_CS | SD片选及U601 /1OE |
| J606 | 4 | 右排，USB端向天线端 | D9 | 18 | NANO_D9_DIN | 经R616到U602 DIN |
| J606 | 5 | 右排，USB端向天线端 | D8 | 17 | NANO_D8_LRCLK | 经R615到U602 LRCLK |
| J606 | 6 | 右排，USB端向天线端 | D7 | 10 | NANO_D7_BCLK | 经R614到U602 BCLK |
| J606 | 7 | 右排，USB端向天线端 | D6 | 9 | NANO_D6_IRQ | Nano→U401-6→FPGA事件输入 |
| J606 | 8 | 右排，USB端向天线端 | D5 | 8 | NANO_D5_CTS | FPGA RTS→R407→Nano CTS |
| J606 | 9 | 右排，USB端向天线端 | D4 | 7 | NANO_D4_RTS | Nano RTS→U401-4→FPGA CTS |
| J606 | 10 | 右排，USB端向天线端 | D3 | 6 | NANO_D3_RX | FPGA TX→R406→Nano RX |
| J606 | 11 | 右排，USB端向天线端 | D2 | 5 | NANO_D2_TX | Nano TX→U401-2→FPGA RX |
| J606 | 12 | 右排，USB端向天线端 | GND | — | GND | 载板共地 |
| J606 | 13 | 右排，USB端向天线端 | RST | — | NANO_RST | Q601漏极，低电平复位 |
| J606 | 14 | 右排，USB端向天线端 | D0/RX | 44 | NANO_RX | J605-3调试接收 |
| J606 | 15 | 右排，USB端向天线端 | D1/TX | 43 | NANO_TX | J605-2调试发送 |