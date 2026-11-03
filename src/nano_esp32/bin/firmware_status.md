# 当前固件状态

Nano ESP32 **0.2.3** 于2026-11-02编译通过，包含十三个语音资源ID，新增七条操作提示，保留0.2.2的NFC诊断和原有保护。应用镜像Checksum及Validation Hash有效；输入和产物SHA256见[构建验证](../../../data/nano_build_validation.json)。

Sketch uses 973865 bytes (30%) of program storage space. Maximum is 3145728 bytes.；Global variables use 103276 bytes (31%) of dynamic memory, leaving 224404 bytes for local variables. Maximum is 327680 bytes.

当前0.2.3尚未烧录。此前已于2026-11-02在Nano 7C0C5F54D5EC（COM18）验证的是0.2.2，仅USB运行及诊断命令回复通过，外设未验收。新WAV需复制到J603卡`/voice/`；七条新增资源未接入自动触发，采集提示不能与测量或声学窗口重叠。NFC/SD原故障未定位。

板型Arduino Nano ESP32 ABX00083，板包2.0.18-20240930.arduino3，Pin Numbering默认Arduino、USB TinyUSB、默认分区。按[固件README](../../../docs/nano_build.md)编译或烧录。分段BIN、ELF和匹配分区见本目录，不能不带对应地址混写。
