# 模块上电前设置：麦克风SEL与NFC拨码

本页补充模块自身必须设置的引脚/开关；不改变PCB或程序。先断电设置模块，再按[逐脚接线总表](wiring.md)连接。

## 原理图与PCB原生工程在哪里

[嘉立创EDA专业版原生epro2工程](../hardware/pcb/jianwei.epro2)已经上传，包含主载板原理图、主载板PCB、前面板原理图、前面板PCB及工程库。下载后在嘉立创EDA专业版导入工程，在工程树选择对应板件；主载板原理图8页，前面板1页。不同板的J101等同名位号须带板名区分。

2026-10-27核对GitHub文件大小2,689,861字节、Git blob SHA1为`b5e2830cbe0dc55166b604559171f8f93acb0728`，与本地原生工程一致；ZIP结构完整，工程内部检索到两块PCB及两份原理图的文档标题。此项不等同重新在EDA运行ERC/DRC。

两板Gerber在[硬件工程与制造目录](../hardware)，原理图及PCB网表在[网表与逐脚查询目录](../hardware/netlists)。Gerber是制造输出，不代替可编辑工程；已归档Gerber早于工程最后丝印更新，重新生产应从最终工程导出。

### 麦克风SEL（L/R）怎样接

本项目单麦克风方案采用SEL→模块GND（与J706-2同地），在模块端短接即可；SEL不占J706额外针脚，原四根线仍为1=3V3、2=GND、3=CLK、4=DAT。SEL不接CLK、DAT或5V，不悬空。

这里按模块SEL直接连接MP34DT01的L/R脚解释。若模块已用焊桥/电阻把SEL固定到3.3V，先核对模块电路，不再把它硬短接GND；可保留固定高并使用对应采样相位。模块是否带反相/固定连接仍以其原理图或导通测量为准，不能由芯片手册推出所有模块内部接法。

| SEL / L/R电平 | 芯片数据有效阶段 | 接收配置含义 |
|---|---|---|
| GND（本方案） | CLK低电平期间；高电平期间高阻 | 现有自动相位检测可用；固定相位时对应phase=low |
| 模块3V3/VDD | CLK高电平期间；低电平期间高阻 | 对应phase=high，或使用自动检测 |

SEL只是PDM声道/有效半周期选择，不是静音、增益或片选。CLK由PL输出2MHz；单只麦克风也须固定SEL。ST MP34DT01手册§7.1、表8（PDF第14页）给出L/R规则：[仓库内手册](datasheets/mp34dt01.pdf)、[ST原稿来源](https://media.digikey.com/pdf/data%20sheets/st%20microelectronics%20pdfs/mp34dt01.pdf)。

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
