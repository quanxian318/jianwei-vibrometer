# Nano ESP32构建验证

当前 **0.2.3** 已于2026-11-02使用实际Nano板包编译通过。语音资源表包含十三个 ID，配合 PS 事件调用。输入与构建记录逐个校验SHA256，ELF含全部十三个ID，镜像校验及摘要有效。

- 构建输出：Sketch uses 973865 bytes (30%) of program storage space. Maximum is 3145728 bytes.；Global variables use 103276 bytes (31%) of dynamic memory, leaving 224404 bytes for local variables. Maximum is 327680 bytes.
- [完整记录](../data/nano_build_validation.json)：输入及产物哈希、十三条WAV哈希、实测边界。
- 镜像检查；主机检查。NFC控制回归按当前资源表重新编译通过；未改动的控制台、WAV解析及网关回归复用已有结果。

0.2.3 已烧录到 Nano，USB ping/pong 和 status 版本回复正常。十三条 WAV 已写入 FAT32 语音卡并全部读回校验。Nano 与 AX7010 通信、NFC 读卡、SD 语音、APP 网关均已完成整机联调。基线采集完成时播放 baseline_done，测量结束调用 measure_done，报警按判级播放对应语音。
