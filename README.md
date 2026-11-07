# Baud

基于 Qt 6 Widgets 与 QCustomPlot 的 Windows 串口上位机，用于接收、解析与可视化 [VOFA+](https://www.vofa.plus/) 协议数据。

源码仓库：https://github.com/AdonisZeng/Baud

## 功能（M1）

- 串口参数配置：端口、波特率（可自定义）、数据位、校验、停止位、流控。
- 三种 VOFA+ 协议：
  - **JustFloat**：小端 float32 数组 + 固定帧尾 `00 00 80 7F`。
  - **FireWater**：CSV 文本行，逗号分隔。
  - **RawData**：原始字节流，按 ASCII / HEX 查看。
- 实时波形：QCustomPlot 深色示波器风格，图例、自动 Y 轴、滚轮缩放、拖动、双击复位、暂停/恢复。
- 通道面板：显示/隐藏、颜色、当前值、最近样本最小/最大值。
- 发送栏：ASCII / HEX、CR/LF/CRLF。
- 串口收发与解析运行在独立线程，界面按批次刷新。
- 构建后自动执行 windeployqt，Release 目录可直接拷贝运行。

## 构建环境

- Windows 10/11 x64
- Visual Studio 2026（v145 工具集）
- Qt 6.11.1 msvc2022_64
- Qt VS Tools
- Qt 模块：`core;gui;widgets;serialport;printsupport`

Qt 默认安装路径按 `D:\Software\Qt\6.11.1\msvc2022_64` 配置；如不同，请同步修改 `Baud.vcxproj` 中 `windeployqt`、VC 运行库路径，以及 Qt VS Tools 的 `qt6_msvc` 注册路径。

```powershell
& "D:\Software\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "Baud.slnx" /p:Configuration=Debug /p:Platform=x64 /m
```

Release：

```powershell
& "D:\Software\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "Baud.slnx" /p:Configuration=Release /p:Platform=x64 /m
```

产物：

- Debug：`x64\Debug\Baud.exe`
- Release：`x64\Release\` 整个目录（包含 Qt DLL、插件与 VC 运行库）

## 目录结构

```text
Baud.cpp / Baud.h / main.cpp   主窗口与程序入口
src/protocol/                  协议类型、JustFloat / FireWater 解析器
src/serial/                    串口工作线程与参数
src/model/                     通道环形缓冲
src/ui/                        QCustomPlot 波形控件与主题
qcustomplot/                   QCustomPlot 2.1.1 官方源码（GPLv3）
LICENSES/                      Qt LGPL-3.0 许可文本
```

## 协议说明

| 协议 | 格式 | 说明 |
|---|---|---|
| JustFloat | 小端 float32 数组 + `00 00 80 7F` | 帧尾是 `+Inf` 的 float 字节表示；若某通道数据恰为 `+Inf` 会与帧尾冲突，属协议固有限制。 |
| FireWater | 每行 CSV，以 `\n` 结束 | 支持 `\r\n`；空行和非法数值行会被丢弃。 |
| RawData | 原始字节流 | 不做帧解析，仅按 ASCII/HEX 显示。 |

协议参考：[VOFA-Protocol-Driver](https://github.com/skjnsnb/VOFA-Protocol-Driver)（MIT）。本仓库未复制其源码。

## 快速验证

USB-TTL 的 TX 与 RX 短接后，在发送栏使用回环数据验证：

- JustFloat（HEX）：`00 00 80 3F 00 00 00 40 00 00 80 7F` → 2 通道，值 1.0 / 2.0。
- FireWater（ASCII）：`1,2,3` + CRLF → 3 通道，值 1 / 2 / 3。
- RawData（ASCII）：`Hello` → 监视页显示 `Hello`。

## 许可证

本项目以 **GNU General Public License v3.0 or later（GPL-3.0-or-later）** 发布，完整许可文本见 [LICENSE](LICENSE)。

第三方组件声明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)，主要包括：

- **QCustomPlot 2.1.1**：GPL-3.0-or-later，源码位于 `qcustomplot/`，许可文本见 `qcustomplot/GPL.txt`。
- **Qt 6.11.1**：LGPL-3.0-only / GPL-3.0-only 双许可（另有商业许可）；本项目动态链接 Qt，不修改 Qt 源码，LGPL 文本见 `LICENSES/LGPL-3.0.txt`。
- **VOFA-Protocol-Driver**：MIT，仅作协议参考。

如果分发二进制版本（如 `x64\Release` 目录），请同时附带 `LICENSE`、第三方许可文本与源码获取方式；本项目源码发布于 https://github.com/AdonisZeng/Baud ，Qt 采用动态链接，用户可替换 Qt DLL。若未来希望改用 MIT/Apache 或闭源发布，需要购买 QCustomPlot 商业许可，或替换为自绘/其他授权兼容的绘图实现。
