# 板端运行

`artifacts/printer_uart.bit.bin` 为当前 PL 位流，`artifacts/vibmond.gz` 为 PS 程序快照。Nano 固件位于 `src/nano_esp32/bin`，SD 语音资源位于 `src/nano_esp32/sd_card/voice`。

1. 使用已为视频帧缓存预留 DDR 顶端内存的 AX7010 Linux 镜像，接入 AN5642 和 HDMI 屏。
2. 停止相机和界面进程，按 `src/ax7010/linux/vibmon/tools/prepare_an5642_runtime.py --help` 设置位流路径、加载 PL 并设置时钟。
3. 以 `src/ax7010/linux/vibmon/config/current_board.example.json` 为起点，核对设备路径、外设和标定文件，保存为板上的 `board.json`。
4. 解压程序并运行：

```sh
gzip -dk vibmond.gz
chmod +x vibmond
./vibmond -c board.json
```

完整参数、标定设置及部署版本见 [构建与部署](../docs/current/build_deploy.md)。默认源码采用布局绑定标定；代码包保留历史程序的显式复现工具 `reproduce_legacy_binary.py`。
