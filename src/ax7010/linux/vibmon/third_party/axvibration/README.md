# axvibration

队友的多点测振核心，原样取自 `AX7010_Vibration_Linux_Test_20261021.zip`（SHA256 `75dd53986c979629953e8ab0e834af4e639d1ead3e5faba10174f431daf66e78`）包内 `AX7010_Vibration_Linux_20261021/src/` 的 `portable_vibration.c/.h`、`spatial_core.c`、`vb_runtime.c/.h`，文件未修改。

vibmond 通过 `src/measure/multipoint.*` 调用，不使用原包的命令行程序、V4L2 采集器和预编译二进制。更新时整体替换这五个文件并运行 `make test`：`test_multipoint` 核对封装与原包整帧模糊算法逐帧逐位一致。
