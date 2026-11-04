# 整机装配：逐引脚接线与模块互连

适用主载板100×100mm、前面板60×40mm及2026-10-20嘉立创工程。本文按当前选定模块统一装配入口；数据手册、采购链接、完整针序和待确认项集中在这里。PCB端的针号、原始网络名和坐标来自云端两板最新工程提取CSV，本轮逐字节核对与GitHub一致。模块端写信号名；没有该批次厂家针图时，不编造模块物理针号或线色。

- [逐根线CSV](wire_harness.csv)：可用于制线与通断登记。
- [12项淘宝链接与所选规格](module_specs.md)。
- [数据手册、原厂入口与资料缺口](datasheets/README.md)。
- [两板全部焊盘查询](../hardware/netlists/pin_browser.html)。
- [实物方向和1脚位置](carrier_pin_orientation.csv)；该表只借用硬件方向，功能以本文为准。

## 原理图与PCB原生工程在哪里

[嘉立创EDA专业版原生epro2工程](../hardware/pcb/jianwei.epro2)已经上传，包含主载板原理图、主载板PCB、前面板原理图、前面板PCB及工程库。下载后在嘉立创EDA专业版导入工程，在工程树选择对应板件；主载板原理图8页，前面板1页。不同板的J101等同名位号须带板名区分。

2026-10-27核对GitHub文件大小2,689,861字节、Git blob SHA1为`b5e2830cbe0dc55166b604559171f8f93acb0728`，与本地原生工程一致；ZIP结构完整，工程内部检索到两块PCB及两份原理图的文档标题。此项不等同重新在EDA运行ERC/DRC。

两板Gerber在[硬件工程与制造目录](../hardware)，原理图及PCB网表在[网表与逐脚查询目录](../hardware/netlists)。Gerber是制造输出，不代替可编辑工程；已归档Gerber早于工程最后丝印更新，重新生产应从最终工程导出。

## 整机互连

```text
12V适配器 → 主载板J101 → 保险/保护 → J102上的SW101 → 12V母线
主载板J201（5V） ─ USB-A转DC5521直通线 → AX7010电源座
主载板J801 ─ 40P IDC直通排线 → AX7010 J11（信号与3.3V来源）
AN5642双目相机 ─ 原配连接 → AX7010 J10
AX7010 HDMI ─ HDMI线 → 7inch HDMI LCD(H) HDMI
主载板J501（5V） ─ 电源线 → 屏幕Power Only
AX7010 USB HOST ─ USB数据线 → 屏幕Touch
主载板J502 ─ 10P逐针直通 → 前面板J101
Nano ESP32 ABX00083 ─ 两排15针 → 主载板J604/J606
Nano ─ 板内SPI → J602 PN532及J603 microSD
Nano ─ 板内I2S → MAX98357A ─ J601 → 无源喇叭
AX7010 ─ J801及板内总线 → J701 ToF/J702 IMU/J703参考加速度/J704温湿度
AX7010 ─ J801-32/33 → J706 PDM麦克风（PL接收、PS处理）
AX7010 ─ 板内控制/串口 → J401打印机、J301照明、J304十字投影、J302声光/风扇
```

各传感器分别接主载板，不需要额外把温湿度模块接到ToF模块，或把NFC接到AX7010。共用总线已经在PCB连好：J701/J704共I2C1，J702/J703共SPI；片选和中断独立。Nano与AX7010之间的UART、RTS/CTS和IRQ也在PCB内，不另拉杜邦线。麦克风不接Nano/I2S功放，不与喇叭并联。

J201/J501都只输出电源：USB电气针1=5V、4=GND、2/3不接；屏幕触摸数据必须接AX7010 USB HOST。AX7010 J11-2在载板NC，不能把排线当5V供电线。AX7010 J11-39/40提供AX_3V3_RAW到载板，再生成相关3.3V域，不能与Nano的3V3脚或外部3.3V源直接并联。

整机J101按12V、DC5525、内正外负使用；AX7010按5V、DC5521使用。仅按既定插座/供电方案连接，不能互换两路电压。

## 针号和观察方向

所有表中的1、2、3都是PCB电气针号，不统一声称“面对插头最左就是1”。按板上1脚、方焊盘与现有方向图核对；插头插合面、元件面、背面图左右可能相反。线束CSV提供元件面PCB坐标用于对照。GH连接器的额外接地机械焊脚不作为线芯。

40P线要求1→1…40→40，2.54mm IDC母对母；10P前面板线要求1→1…10→10。卖家“同向/反向/凸点朝外”不替代通断表；本次不把任何一款按照片判断为直通。模块端排针顺序不同，就按下面信号名重排线束。


## VL53L1X：J701

PS/PL；I2C1，0x29。只拉低或释放XSHUT。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | 3V3_SENS | VIN/3V3 | 3.3V受控传感器电源 |
| 2 | GND | GND | 公共地 |
| 3 | I2C1_SCL | SCL | J801/J11-13 → M20 |
| 4 | I2C1_SDA | SDA | J801/J11-14 → M19 |
| 5 | TOF_INT | GPIO1/INT | J801/J11-7 → G20 |
| 6 | XSHUT_OD | XSHUT | Q701开漏控制，由TCA9555 P0.4驱动 |

## LSM6DSV：J702

PS/PL；SPI Mode 0，固定在相机支架。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | 3V3_SENS | 3V3/VDD | 3.3V受控传感器电源 |
| 2 | GND | GND | 公共地 |
| 3 | SENS_SPI_SCLK | SCL/SPC | 板上源端串阻 → J801/J11-35 → J14 |
| 4 | SENS_SPI_MOSI | SDA/SDI | 板上源端串阻 → J801/J11-36 → K14 |
| 5 | SENS_SPI_MISO | SDO/SA0 | R707 → J801/J11-34 → H15 |
| 6 | IMU_CS | CS | J801/J11-29 → G18 |
| 7 | IMU_INT1_RAW | INT1 | 经RN402串阻 → J801/J11-30 → G17 |
| 8 | IMU_INT2_RAW | INT2 | 缓冲返回 → J801/J11-31 → H17 |

## GY-291 ADXL345：J703

PS/PL；SPI Mode 3；SDO不可硬接GND；INT2可不接。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | 3V3_SENS | 3V3供电脚（按模块原理图） | 3.3V受控传感器电源 |
| 2 | GND | GND | 公共地 |
| 3 | SENS_SPI_SCLK | SCL/SCLK | 与J702-3共用 → J801/J11-35 → J14 |
| 4 | SENS_SPI_MOSI | SDA/SDI | 与J702-4共用 → J801/J11-36 → K14 |
| 5 | SENS_SPI_MISO | SDO | 与J702-5共用 → R707 → J801/J11-34 → H15 |
| 6 | ADXL_CS | CS | J801/J11-27 → E19 |
| 7 | ADXL_DRDY_RAW | INT1/DATA_READY | 缓冲返回 → J801/J11-28 → E18 |
| 8 | NC | 不接 | 不接 |

## GY-SHT40：J704

PS/PL；I2C1，常见0x44；不按排针左右位置直插。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | 3V3_SENS | VIN/3V3 | 3.3V受控传感器电源 |
| 2 | GND | GND | 公共地 |
| 3 | I2C1_SCL | SCL | 与J701-3共用 → J801/J11-13 → M20 |
| 4 | I2C1_SDA | SDA | 与J701-4共用 → J801/J11-14 → M19 |

## MP34DT01 PDM模块：J706

PL接收、PS处理；CLK 2MHz，L/R必须固定。用户已确认型号MP34DT01，现有芯片手册与方案对应此型号；模块SEL/L/R按下文固定接法设置，默认接GND；内部是否已有固定连接仍按实物核对。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | 3V3_SENS | 3V3/VDD | 3.3V受控传感器电源 |
| 2 | GND | GND | 公共地 |
| 3 | I2C2_SCL | CLK | J801/J11-32 → H16，PL输出mic_clk |
| 4 | I2C2_SDA | DAT/DOUT | J801/J11-33 → G15，PL输入mic_dat |

### 麦克风SEL（L/R）怎样接

本项目单麦克风方案采用SEL→模块GND（与J706-2同地），在模块端短接即可；SEL不占J706额外针脚，原四根线仍为1=3V3、2=GND、3=CLK、4=DAT。SEL不接CLK、DAT或5V，不悬空。

这里按模块SEL直接连接MP34DT01的L/R脚解释。若模块已用焊桥/电阻把SEL固定到3.3V，先核对模块电路，不再把它硬短接GND；可保留固定高并使用对应采样相位。模块是否带反相/固定连接仍以其原理图或导通测量为准，不能由芯片手册推出所有模块内部接法。

| SEL / L/R电平 | 芯片数据有效阶段 | 接收配置含义 |
|---|---|---|
| GND（本方案） | CLK低电平期间；高电平期间高阻 | 现有自动相位检测可用；固定相位时对应phase=low |
| 模块3V3/VDD | CLK高电平期间；低电平期间高阻 | 对应phase=high，或使用自动检测 |

SEL只是PDM声道/有效半周期选择，不是静音、增益或片选。CLK由PL输出2MHz；单只麦克风也须固定SEL。ST MP34DT01手册§7.1、表8（PDF第14页）给出L/R规则：[仓库内手册](datasheets/mp34dt01.pdf)、[ST原稿来源](https://media.digikey.com/pdf/data%20sheets/st%20microelectronics%20pdfs/mp34dt01.pdf)。

## PN532 V3：J602

Nano；SPI Mode 0、LSB first。Elechouse V3：开关1 OFF，2 ON；仿制板按其手册。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | 5V_AUX | VCC（模块接受5V） | 5V_AUX电源 |
| 2 | GND | GND | 公共地 |
| 3 | SPI_SCLK_BUS | SCK | R604 → Nano D13/GPIO48 |
| 4 | SPI_MOSI_BUS | MOSI | R602 → Nano D11/GPIO38 |
| 5 | PN_MISO | MISO | U601门控 → R603 → Nano D12/GPIO47 |
| 6 | PN_CS | SS/SSEL | Nano A3/GPIO4 |
| 7 | PN_IRQ | IRQ | R612 → Nano A0/GPIO1 |
| 8 | NC | 不接（不是RSTO） | 不接 |

### PN532 V3拨码怎样设置

本主载板J602使用SPI，由Nano ESP32管理。断电后将模块拨码**1=OFF、2=ON**，再重新上电；不是I2C或HSU串口模式。OFF/ON按开关本体或PCB的ON标记判定，不用照片中的上/下、左/右代替电气状态。

| 模式（Elechouse V3定义） | 拨码1 | 拨码2 | 本项目是否使用 |
|---|---|---|---|
| HSU / UART | OFF | OFF | 否 |
| I2C | ON | OFF | 否 |
| SPI | OFF | ON | 是 |

上述表依据[Elechouse V3原厂手册第3页](https://www.elechouse.com/elechouse/images/product/PN532_module_V3/PN532_%20Manual_V3.pdf)；[仓库内模块手册](datasheets/pn532_module.pdf)。仿制V3若开关编号或电路不同，以本块板丝印/厂家表为准。1/2均ON不是本项目设定，不自行推断其模式。

设置后接J602：1=模块VCC（5V_AUX）、2=GND、3=SCK、4=MOSI、5=MISO、6=SS/SSEL、7=IRQ、8=NC；模块RSTO不接。SPI信号为3.3V逻辑。不要套用手册Arduino UNO的SS=10示例：本项目SS为Nano A3/GPIO4，IRQ为A0/GPIO1；SD另用D10/GPIO21片选。

若软件读不到PN532固件版本，先检查拨码、重新上电、线束针号及模块3.3V SPI逻辑，再排查读卡流程。拨码只选择通信接口，不代替NFC软件初始化。

## EM5820H 12V TTL：J401

PS/PL；主板PH2.0 5P转打印机MX1.25；模块端物理针号待厂家图。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | 12V_PRN | VIN/+12V | 见板上电源/控制通路 |
| 2 | PRN_HOST_CTS | CTS（打印机输出） | U401/RN402 → J801/J11-26 → D19 |
| 3 | PRN_HOST_TX | RX（打印机接收） | J801/J11-24 → M17 → U402发送门控 → 打印机RX |
| 4 | PRN_HOST_RX | TX（打印机发送） | 打印机TX → U401 → J801/J11-25 → D20 |
| 5 | GND | GND | 公共地 |

## LED1 12V照明：J301

12V成品灯、自带限流/驱动；多个照明灯只在本支路并联。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | $3N16 | 灯正端+ | 12V照明支路经F301 |
| 2 | $3N14 | 灯负端−（受控） | LED1低边MOS；PWM来自J801/J11-3 → F17 |

## LED2 12V/650nm十字投影：J304

固定100%开关控制；默认采集时关闭，固定投影不能当作随材料运动的散斑。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | $3N29 | 投影器正端+ | 12V投影支路经F302 |
| 2 | $3N28 | 投影器负端−（受控） | LED2低边MOS；控制来自J801/J11-4 → F16 |

## 组合声光灯及风扇：J302

公共正极12V款。线色以商家图为准；各负端互不并接，也不另接GND。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | 12V_ALM | 经SW302接灯公共正、经SW303接风扇正 | 12V_ALM经F304 |
| 2 | ALM_R_N | 红灯控制负端 | TCA9555 P1.0 → 红灯低边MOS |
| 3 | ALM_Y_N | 黄灯控制负端 | TCA9555 P1.1 → 黄灯低边MOS |
| 4 | ALM_G_N | 绿灯控制负端 | TCA9555 P1.2 → 绿灯低边MOS |
| 5 | BZ_N | 有源蜂鸣器控制负端 | TCA9555 P1.3 → 蜂鸣器低边MOS |
| 6 | FAN_N | 风扇负端 | TCA9555 P1.5 → 风扇低边MOS |

## 8Ω/3W无源喇叭：J601

MAX98357A差分输出，两根都不是GND；额定3W不是保证输出3W。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | SPK_P | 喇叭正端 | Nano I2S → 板载MAX98357A → SPK_P |
| 2 | SPK_N | 喇叭负端 | 板载MAX98357A → SPK_N（不是GND） |

## 可选转速探头：J705

只接受既定0–3.3V/0–5V脉冲；不接12/24V输出，未选购可空置。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | 5V_AUX | +5V | 5V_AUX电源 |
| 2 | GND | GND | 公共地 |
| 3 | $7N56 | 脉冲SIG | 输入整形/缓冲 → J801/J11-15 → K18 |

## 可选3.3V USB-TTL调试：J605

无供电脚，不接适配器5V/3V3；TX/RX以两端方向配对。

| 针号 | PCB实际网名 | 外部接到哪里 | 控制/处理去向 |
|---|---|---|---|
| 1 | GND | GND | 公共地 |
| 2 | NANO_TX | 适配器RX | Nano D1/GPIO43 → 外部适配器RX |
| 3 | NANO_RX | 适配器TX | 外部适配器TX → Nano D0/GPIO44 |

Nano固件0.2.1起默认启用J605：115200、8N1、无流控，命令与USB-C相同；发送`ping`返回`pong`，`help`列出命令，`status`返回状态JSON。两端均输出日志与命令回复；`baud`和`flow`仅修改AX7010链路，不改变J605。接线完成不代表该版本已通过实物验证，见[Nano固件说明](../src/nano_esp32/README.md)。

## 前面板：主板J502 ↔ 小板J101

GH1.25 10P逐针对接。3脚的差异是设计事实：主板有AX_3V3，小板该触点NC；不外加跳线。

| 电气针号 | 主板J502网络 | 小板J101网络 | 用途 |
|---|---|---|---|
| 1 | 5V_AUX | 5V_AUX | 状态灯5V供电 |
| 2 | GND | GND | GND |
| 3 | AX_3V3 | NC | 小板NC，不接测试点/不供电 |
| 4 | GND | GND | GND |
| 5 | ENC_A_RAW | ENC_A | 编码器A |
| 6 | ENC_B_RAW | ENC_B | 编码器B |
| 7 | ENC_SW_RAW | ENC_SW | 编码器按压 |
| 8 | KEY_K1_RAW | KEY_K1 | K101按键 |
| 9 | WS_DATA_5V | WS_DATA_5V | HL101→HL105灯数据 |
| 10 | GND | GND | GND |

## 总开关、允许开关与按钮指示灯

| 开关/接口 | 触点接法 | 灯脚接法（灯与触点独立） |
|---|---|---|
| SW101总电源 | J102-1（$1N3）↔COM/NO↔J102-2（12V_PROTECTED）；两针都不是GND | 12V灯+接J102-2，灯−接J306-2 GND |
| SW301照明/投影允许 | J305-1 LED_EN↔COM/NO↔J305-2 GND；闭合允许 | 采用5V灯：灯+接J306-1 5V_AUX，灯−接J305-1 LED_EN；不可接12V |
| SW302外部声光允许 | J302-1→COM/NO→组合灯公共正极 | 12V灯+接SW302输出，灯−接J306-2 GND |
| SW303风扇允许 | J302-1→COM/NO→风扇正极 | 12V灯+接SW303输出，灯−接J306-2 GND |

COM/NO/LED+/LED−的实际金属端子编号须按这批按钮厂家图；购买链接不能证明某脚号就是COM。SW301现有灯接法使用5V，若买的是12V灯不能直接套用并保证亮度。SW101触点按整机DC电流选择（设计5A，原采购建议留至≥6A DC余量），不把按钮的AC标称或灯电压当作触点DC能力。

J306仅1=5V_AUX、2=GND，没有12V。SW302/SW303是外部线束分支，开关之后的两条正线不再并接。三色灯与蜂鸣器需公共正极12V、四路独立低边输入，线色仍待厂家图。

## AX7010的40针和Nano的30针

主板J801与AX7010 J11电气编号逐针直通，完整表如下。这里的输入/输出以AX7010为参照；32/33虽保留旧网名I2C2，但用途已改PDM。

| 针号 | 网络 | HDL端口 | FPGA封装脚 | AX方向 | 初始要求 |
|---|---|---|---|---|---|
| 1 | GND |  |  | 电源/地/保留 |  |
| 2 | NC |  |  | 电源/地/保留 |  |
| 3 | LED1_PWM | led1_pwm | F17 | 输出 | PWM/数据低 |
| 4 | LED2_PWM | led2_pwm | F16 | 输出 | PWM/数据低 |
| 5 | FP_KEY | k1 | F20 | 输入 |  |
| 6 | WS_DATA | ws_data | F19 | 输出 | PWM/数据低 |
| 7 | TOF_INT | tof_int | G20 | 输入 |  |
| 8 | RTC_1HZ | rtc_1hz | G19 | 输入 |  |
| 9 | AX_PFAIL_N | pfail_n | H18 | 输入 |  |
| 10 | EXP_INT_N | exp_int_n | J18 | 输入 |  |
| 11 | I2C0_SCL | i2c0_scl | L20 | 开漏双向 |  |
| 12 | I2C0_SDA | i2c0_sda | L19 | 开漏双向 |  |
| 13 | I2C1_SCL | i2c1_scl | M20 | 开漏双向 |  |
| 14 | I2C1_SDA | i2c1_sda | M19 | 开漏双向 |  |
| 15 | TACH_IN | tach_in | K18 | 输入 |  |
| 16 | ENC_SW | enc_sw | K17 | 输入 |  |
| 17 | ENC_A | enc_a | J19 | 输入 |  |
| 18 | ENC_B | enc_b | K19 | 输入 |  |
| 19 | ESP_TXD | esp_txd | H20 | 输出 | UART空闲高 |
| 20 | ESP_RXD | esp_rxd | J20 | 输入 |  |
| 21 | ESP_RTS | esp_rts | L17 | 输出 |  |
| 22 | ESP_CTS | esp_cts | L16 | 输入 |  |
| 23 | ESP_IRQ | esp_irq | M18 | 输入 |  |
| 24 | PRN_TXD | prn_txd | M17 | 输出 | UART空闲高 |
| 25 | PRN_RXD | prn_rxd | D20 | 输入 |  |
| 26 | PRN_CTS | prn_cts | D19 | 输入 |  |
| 27 | ADXL_CS | adxl_cs | E19 | 输出 | 片选高 |
| 28 | ADXL_DRDY | adxl_drdy | E18 | 输入 |  |
| 29 | IMU_CS | imu_cs | G18 | 输出 | 片选高 |
| 30 | IMU_INT1 | imu_int1 | G17 | 输入 |  |
| 31 | IMU_INT2 | imu_int2 | H17 | 输入 |  |
| 32 | I2C2_SCL（PDM CLK） | mic_clk | H16 | 推挽输出 | 2MHz；禁用时低 |
| 33 | I2C2_SDA（PDM DAT） | mic_dat | G15 | 输入 | 麦克风数据 |
| 34 | SPI_MISO | spi_miso | H15 | 输入 |  |
| 35 | SPI_SCLK | spi_sclk | J14 | 输出 |  |
| 36 | SPI_MOSI | spi_mosi | K14 | 输出 |  |
| 37 | GND |  |  | 电源/地/保留 |  |
| 38 | GND |  |  | 电源/地/保留 |  |
| 39 | AX_3V3_RAW |  |  | 电源/地/保留 |  |
| 40 | AX_3V3_RAW |  |  | 电源/地/保留 |  |
Nano必须是ABX00083，USB端向天线端依次1…15，元件面俯视左J604、右J606。以下为载板插座脚号，不是ESP32芯片封装号：

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
## 共享总线与供电条件

- PN532与Nano辅助SD共享Nano SPI，PN532 Mode 0/LSB first，SD使用其SD事务配置及MSB first；各自片选一次只拉低一个，回传MISO经U601门控。J603无需外接跳线；辅助卡不等同AX7010的系统启动卡。
- LSM6DSV与ADXL345共享传感器SPI，分别Mode 0与Mode 3，两CS高时再切换模式。GY-291若SDO硬接GND不能接共享MISO；ADXL345在CS高时误识别I2C的资格测试说明见[模块接入指南](module_drivers.md)。
- SHT40和VL53L1X共I2C1；模块电源/上拉须与3.3V兼容。J706现在不接I2C模块，原4.7k上拉保留不等于它仍是I2C。
- J301-1=$3N16、J304-1=$3N29是正端；J301-2=$3N14、J304-2=$3N28才是受控负端。负端不能与固定GND、外壳地或另一负载负端短接。
- 打印机J401-3 HOST_TX→打印机RX，J401-4 HOST_RX←打印机TX。若商家TX/RX已按主机方向标注，先核对含义；不是再机械地交叉一次。当前没有打印机端物理针号证据，不能把本表1…5直接套到其MX1.25壳体。

## 还缺哪些装配证据

PCB端针序与网表可查，不等于每个商品SKU已经完成实物验证。仍需取得：麦克风SEL是否已有内部固定连接及模块电路（型号已确认MP34DT01）；三色灯公共极性/蜂鸣器/线色；按钮端子图和各灯电压；打印机这批MX1.25针图/TTL流控协议/峰值电流；GY-291的SDO绑线和供电路径。通用芯片手册不能代替这些模块资料。

本次只整理文档和对照表，不改程序、RTL、PCB或板端状态。2026-10-25程序冻结标签仍为`freeze-20261025-capture-near-stereo`；它不是当前main自动保持不变的承诺。后续云端新增APP等代码以对应提交为准。历史ZIP按其日期视为快照，当前接线从本入口读取。
