# 已选模块接入与驱动

模块SEL与PN532拨码设置见[上电前设置](module_switches.md)。麦克风SEL直接接L/R的模块，单麦方案SEL接模块GND，先核对没有内部固定到3V3；J706仍用四线。PN532按本块开关ON标记设1 OFF、2 ON，断电设置并重新上电。

当前完整线束及购买链接见[统一装配入口](wiring.md)。

用户确定的基础六种外设：PN532 NFC RFID V3、LSM6DSV、EM5820H 12V版、VL53L1X、GY-SHT40、GY-291 ADXL345。下表为本主载板实际端口定义，必须按功能接线，不能按商品照片的左右位置直接套用。

## 快速接线表

| 主板接口 | 按电气针号排列 | 模块要求 |
|---|---|---|
| J602 PN532 | 1=5V_AUX，2=GND，3=SCK，4=MOSI，5=MISO，6=SS，7=IRQ，8=NC | 支持SPI、3.3V逻辑；供电可接受5V的完整模块 |
| J702 LSM6DSV | 1=3V3_SENS，2=GND，3=SCK，4=MOSI，5=MISO，6=IMU_CS，7=INT1，8=INT2 | 4线SPI，3.3V接口 |
| J703 GY-291 | 1=3V3_SENS，2=GND，3=SCK，4=MOSI，5=MISO，6=ADXL_CS，7=ADXL_DRDY_RAW，8=NC | ADXL345，SDO可用，不被0Ω接地；INT1接7 |
| J701 VL53L1X | 1=3V3_SENS，2=GND，3=SCL，4=SDA，5=GPIO1，6=XSHUT | 确认模块VIN支持3.3V，不把模块输入当裸芯片供电脚 |
| J704 GY-SHT40 | 1=3V3_SENS，2=GND，3=SCL，4=SDA | 3.3V供电，I2C上拉不得到5V |
| J706 PDM麦克风 | 1=3V3_SENS，2=GND，3=CLK，4=DAT；原网名I2C2_SCL/SDA | 3.3V PDM模块，L/R固定；用户已确认MP34DT01，模块L/R接法待核 |
| J401打印机 | 1=12V_PRN，2=CTS(打印机输出)，3=HOST_TX，4=HOST_RX，5=GND | EM5820H 12V TTL版，按本批次说明核对方向 |

J602/J702/J703板端为讯普WAFER-GH1.25-8PWB，GH1.25带扣8P线束；J701/J704等具体连接器型号从BOM及当前工程查阅。J401板端为JST S5B-PH-K-S(LF)(SN)、PH2.0 5P，打印机MX1.25端需要按功能做转接；“5P”相同并不表示能直接插合。

## PN532 V3：Nano端NFC

本板走SPI，不走UART HSU。对应Elechouse V3模块的拨码是1 OFF、2 ON；仿制板按ON标记和实际说明核对，断电设置后重启。J602-8没有连RSTO，不能用它做复位输入；模块RSTO悬空即可。

模块供电5V_AUX，SPI接Nano D13/D11/D12/A3，IRQ到A0。起步1MHz、Mode 0、LSB first，和SD的MSB first不同。驱动先读固件版本，再配置SAM并读ISO14443A UID；默认只用UID绑定测点，写卡/密钥业务不属于基本识别。

用模块厂家示例时检查SPI事务和阻塞超时，不能读卡等待期间占住SD总线。标签靠近金属可能失效，装壳后再做天线距离测试。卡片停留时去重，离开后再进入产生新事件。

参考：[本地V3模块说明](datasheets/pn532_module.pdf)、[PN532芯片资料](datasheets/pn532.pdf)、[模块原厂说明](https://www.elechouse.com/elechouse/images/product/PN532_module_V3/PN532_%20Manual_V3.pdf)。

## LSM6DSV：AX端相机运动IMU

安装在相机支架上，轴向随装配记录。模块SCL/SPC为SPI SCK、SDA/SDI为MOSI、SA0/SDO为MISO；不要把SDO误接成I2C地址跳线。3.3V供电，从Mode 0、1MHz起步，WHO_AM_I寄存器0x0F预期0x70。

通过对应LSM6DSV驱动配置加速度/角速度量程、ODR、BDU及连续读、滤波和INT1/INT2路由；使用该型号寄存器，不套用名称相近LSM6DS3/DSOX的寄存器初始化表。DRDY有效边沿由程序配置并与PL捕获一致，配置后读回关键寄存器。

初始建议加速度±2g、陀螺±250dps、ODR约240Hz作为调试方案，须从该芯片支持的实际档位选择并记录；最终ODR和量程按相机振动与带宽设定。先做静止重力/零角速率与轴向检查，再做运动对照。陀螺/加速度只用于相机运动估计或质量判定，不能未经模型标定直接从图像位移相减。

参考：[LSM6DSV本地手册](datasheets/lsm6dsv.pdf)、[ST产品资料](https://www.st.com/resource/en/datasheet/lsm6dsv.pdf)。

## GY-291 ADXL345：AX端参考加速度计

当前选择ADXL345，不使用原方案ADXL355驱动。两者不是寄存器兼容替代。GY-291只是模块商品名，8针单排款和双排新款接法可能不同。

对应模块丝印：VCC/3V3→J703-1，GND→2，SCL→3(SCK)，SDA→4(MOSI)，SDO→5(MISO)，CS→6，INT1→7；INT2可不接，J703-8为NC。若模块同时有5V、3V3、VS脚，按其真实原理图选供电路径：不能把3.3V直接送到高压降稳压器输入后假定芯片仍有3.3V，也不能把模块5V拉到J703。

接线前测SDO到GND是否被0Ω直接短接、CS是否能独立控制。用户之前展示的某双排“新款”原理图把SDO经R4=0接地，这种出厂接法不能直接接共享MISO；需要按模块原理图解除硬绑并确认电气状态后使用。实际GY-291若不是这个版本，不据图片推断已存在该电阻。

4线SPI用Mode 3、MSB first、1MHz起步；读0x00器件ID预期0xE5。建议初始使用全分辨率、±2g、200Hz，数据单位和灵敏度按当前量程/模式换算。INT1配置DATA_READY，PL捕获相同有效边沿；XYZ读取必须是同一帧。

特别是共享总线：ADXL345在CS高时可能将其他器件SPI流量识别为I2C；厂家建议不能控制这种条件时对SDI增加CS或门隔离（SDI = MOSI OR ADXL_CS）。当前主载板没有该隔离，因此不能只通过“切Mode 3”就保证两模块共用无误。首板先单独验证，再交替/持续读取LSM6DSV与ADXL345，检查误响应、丢样和MISO冲突；若失败，修改模块侧适配或调整接口方案，不把错误掩盖在驱动重试里。

参考：[ADXL345官方本地手册](datasheets/adxl345.pdf)，SPI和多器件共享说明见第14页、图36；[ADI产品页](https://www.analog.com/en/products/adxl345.html)。ADXL345是参考观测，不等同经过校准的计量标准。

## VL53L1X：AX端距离

走I2C1，7位地址0x29。ST部分示例将地址写成8位0x52，在Linux 7位API中使用0x29。起步100kHz，GPIO1接TOF_INT，XSHUT接主板开漏关断。TCA P0.4=1把XSHUT拉低，=0释放。

用VL53L1X对应ULD/API移植I2C读写和延时层；不是VL53L0X驱动。先识别芯片、等待启动、初始化、选距离模式/时间预算/测量周期、启连续测量，收到就绪后读取结果及状态并清中断。可从50ms预算、100ms周期调试，再按实际回光条件优化。

失效状态、低信号、超量程不记为0mm有效距离。距离用于工作距离记录或辅助检查，不能代替相机尺度标定。装壳有玻璃时按模块要求做串扰/偏移校准。

参考：[本地手册](datasheets/vl53l1x.pdf)、[STSW-IMG009官方驱动入口](https://www.st.com/en/embedded-software/stsw-img009.html)。官方驱动源码须按ST网站流程取得，包中没有冒充已下载的库。

## GY-SHT40：AX端温湿度

本板J704为3.3V接口。模块VIN、GND、SCL、SDA按功能连；无论紫板黑板，先核对是否有去耦、上拉、稳压和电平转换。裸SHT40不能直接供5V；商品称模块3.3～5V不代表总线可上拉5V。

地址先按0x44版本识别，100kHz起步。模块若带I2C上拉，与主板4.7k并联后阻值降低，测上升沿/低电平电流后决定保留，不按颜色判断好坏。常规1Hz环境记录即可，高精度单次命令后等转换完成再读6字节并分别验证两个CRC8；拒绝CRC错误数据。

温湿度分别按SHT4x手册公式换算，湿度显示按有效范围处理，保留原始值和状态。常规测量关闭加热器，远离主板Buck、功放和排风热流。模块上若没有有效去耦，要按模块/芯片要求补齐，不能仅凭主板远端电容替代。

参考：[SHT4x手册](datasheets/sht4x.pdf)、[Sensirion SHT40官方页面](https://sensirion.com/products/catalog/SHT40)。

## EM5820H：AX端打印

用户确认12V版。本板输出12V_PRN，不适用5～9V版本。厂商同型号有USB/RS232/TTL等选项；当前接口使用TTL，不能连接RS232正负电压到板上的TX/RX/CTS。针序、TTL电平和流控按实物版本说明再核对，RS232不能仅靠软件改成TTL。

板端J401为PH2.0 5P；打印机若是MX1.25 5P，做PH2.0转对应MX1.25线束。J401-3是主机输出到打印机RX，4是打印机输出到主机RX；不要只看商家TX字样盲目交叉。1和5同时承担打印供电回路，按实测峰值选择线规/触点，不能把PTC保持电流当精确限流。

官方页面列58mm纸宽、384点/行、默认9600，可作为初始核对项；最终读自检页和该批次命令手册。供电等待、断电等待、CTS有效状态、缺纸查询、位图命令和中文编码仍需该批次文档或实机验证。详细软件状态机见AX实施说明。

参考：[厂家产品页](https://en.donnwei.com/?page_id=22281&post_type=products)、[厂家下载页](https://www.donnwei.com/?p=41353)、官方驱动ZIP。ZIP中只有Windows安装程序，本次未运行；它不包含串口协议，不能拿来当Linux TTL驱动源码。

## AN5642、屏幕、音频与其他配件

AN5642的DVP/配置走AX7010 J10，沿厂家基础工程驱动；不要移到本载板J801。微雪7inch HDMI LCD(H)通过HDMI显示、USB触摸、单独5V供电；两条USB功能分别接，载板USB-A不承载数据。

MAX98357A已在主板，无需另买功放板；J601接8Ω/3W两线无源喇叭。声光报警蜂鸣器为另一路12V有源负载，接J302，不播放语音。RTC芯片已在板上，仅配CR1220电池。

可选转速探头接J705：5V供电、输出0～3.3V或0～5V脉冲；没有裸OC上拉时按设计选装R702。未购买/未接入的转速和麦克风不能显示为真实有效数据。J706原I2C2现复用PDM，不能再接I2C扩展。
