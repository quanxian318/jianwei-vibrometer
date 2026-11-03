# 见微测振APP

[使用、安装、三平台构建及API说明](../../docs/app_guide.md)。

- `web`：三平台共用界面与HTTP客户端。
- `android`、`ios`、`windows`：各自原生外壳。
- `dist`：当前安装包；iOS包需自行签名安装。
- `tools`：构建与PCB纹理生成脚本。

[三侧对接审查](../../docs/interface_review.md)记录已复现的声学字段和连续设置问题，本次目录整理未修改界面逻辑。PCB纹理生成脚本读取根目录`01_硬件工程`的Gerber、BOM及丝印坐标。
