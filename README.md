# Baud

[English](#english) · [简体中文](#zh-cn)

<a id="english"></a>
Baud is a Windows serial-port host application for devices that send data with the [VOFA+](https://www.vofa.plus/) protocol. It receives, parses and plots data in real time using Qt 6 Widgets and QCustomPlot. The interface is currently in Chinese.

## Features

- Serial setup: port, editable baud rate (common presets from 1,200 to 3,000,000), data bits, parity, stop bits and flow control.
- Three VOFA+ protocol modes:
  - **JustFloat** — little-endian `float32` array terminated by `00 00 80 7F`.
  - **FireWater** — comma-separated text lines.
  - **RawData** — raw byte stream, displayed as ASCII or HEX.
- Real-time plot: dark oscilloscope theme, legend, automatic Y range (until zoomed manually), wheel zoom on both axes, drag to pan, double-click to reset, pause and resume.
- Channel table: visibility, color, current value and min/max of recent samples.
- Send panel: ASCII or HEX input with CR/LF/CRLF line endings.
- Serial I/O and parsing run in a worker thread; the UI refreshes in batches.
- Ring buffer keeps up to 200,000 samples per channel.
- Update check: **帮助 → 检查更新** queries GitHub Releases (also once per day at startup). Installed builds download the installer, verify its SHA256 and start it silently; portable builds open the release page.

## Protocol notes

| Protocol | Format | Notes |
|---|---|---|
| JustFloat | Little-endian `float32` array + `00 00 80 7F` | The terminator is the byte representation of `+Inf`. A channel value that happens to be `+Inf` collides with the terminator; this is a limitation of the protocol. |
| FireWater | One CSV line per frame, ended by `\n` | `\r\n` is accepted. Empty lines and lines with invalid numbers are discarded. |
| RawData | Raw byte stream | No frame parsing; displayed as ASCII or HEX. |

The channel count (1–64) is recalibrated when early frames disagree, so opening a port mid-frame does not corrupt the session. After four consecutive matching frames it is locked, and later frames with a different channel count are dropped.

Protocol reference: [VOFA-Protocol-Driver](https://github.com/skjnsnb/VOFA-Protocol-Driver) (MIT). No source code from that project is included here.

## Build from source

Requirements:

- Windows 10/11 x64.
- Visual Studio 2026 with the MSVC v145 toolset and [Qt VS Tools](https://marketplace.visualstudio.com/items?itemName=TheQtCompany.QtVSTools). The project targets `v145`; retarget the toolset if you use a different Visual Studio version.
- Qt 6.11.1 `msvc2022_64` with the modules `core`, `gui`, `widgets`, `serialport`, `printsupport` and `network`.
- QCustomPlot 2.1.1 is vendored in `qcustomplot/`; no separate download is needed.

The project is a Qt VS Tools MSBuild project, not CMake or qmake. It builds x64 Debug and Release only.

Before building, adjust the local paths in [Baud.vcxproj](Baud.vcxproj):

- Register your Qt installation in Qt VS Tools under the name `qt6_msvc`, or change `<QtInstall>` to your own version name.
- The post-build `windeployqt` command points at `D:\Software\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe`, and the Release build copies the VC runtime from `$(VCInstallDir)Redist\MSVC\14.51.36231`. Update both paths to match your Qt and Visual Studio installations.

Build from a Developer PowerShell:

```powershell
msbuild Baud.slnx /p:Configuration=Debug /p:Platform=x64 /m
msbuild Baud.slnx /p:Configuration=Release /p:Platform=x64 /m
```

Output:

- Debug: `x64\Debug\Baud.exe`
- Release: the entire `x64\Release\` directory. `windeployqt` deploys the Qt DLLs and plugins, so the directory can be copied and run as-is.

## Usage

1. Connect your serial device, launch Baud and refresh the port list.
2. Select the port, baud rate and frame settings, choose the protocol used by your device, then click **连接** (Connect).
3. The right pane switches automatically: the waveform view for JustFloat and FireWater, the raw monitor for RawData.
4. Plot controls: wheel to zoom both axes, drag to pan, double-click to reset the view and restore automatic Y scaling. **暂停/继续** pauses or resumes updates; **清空** clears collected data.
5. In the send panel, choose ASCII or HEX, select a line ending, type the payload and click **发送** (Send).
6. **帮助 → 检查更新** checks GitHub Releases manually; the same check runs once per day shortly after startup.

## Loopback test

Short TX to RX on a USB-TTL adapter, then send the following data from the send panel:

- JustFloat (HEX): `00 00 80 3F 00 00 00 40 00 00 80 7F` → 2 channels with values 1.0 and 2.0.
- FireWater (ASCII): `1,2,3` + CRLF → 3 channels with values 1, 2 and 3.
- RawData (ASCII): `Hello` → `Hello` appears in the raw monitor.

## Project layout

```text
Baud.cpp / Baud.h / main.cpp    main window and entry point
src/protocol/                   protocol types, JustFloat and FireWater parsers
src/serial/                     serial worker thread and settings
src/model/                      per-channel ring buffer
src/ui/                         QCustomPlot waveform widget and theme
src/update/                     GitHub Releases update check, download and installer launch
qcustomplot/                    QCustomPlot 2.1.1 (GPLv3)
LICENSES/                       Qt LGPL-3.0 license text
```

## License

Baud is released under the **GNU General Public License v3.0 or later** (GPL-3.0-or-later). See [LICENSE](LICENSE) for the full text.

Third-party components are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md):

- **QCustomPlot 2.1.1** — GPL-3.0-or-later, vendored in `qcustomplot/`; see `qcustomplot/GPL.txt`.
- **Qt 6.11.1** — LGPL-3.0-only / GPL-3.0-only (commercial licenses also available). Qt is dynamically linked and not modified; see `LICENSES/LGPL-3.0.txt`.
- **VOFA-Protocol-Driver** — MIT, used as a protocol reference only.

If you redistribute a binary build such as `x64\Release`, include `LICENSE`, the third-party license texts and a way to obtain the source code. Source code is available at https://github.com/AdonisZeng/Baud. Qt is dynamically linked, so users can replace the Qt DLLs.

---

<a id="zh-cn"></a>
## 简体中文

Baud 是一款 Windows 串口上位机，用于接收、解析并实时绘制采用 [VOFA+](https://www.vofa.plus/) 协议的数据。基于 Qt 6 Widgets 与 QCustomPlot 构建，界面为中文。

## 功能

- 串口配置：端口、可编辑波特率（常用预设 1,200–3,000,000）、数据位、校验、停止位、流控。
- 三种 VOFA+ 协议：
  - **JustFloat**：小端 `float32` 数组 + 固定帧尾 `00 00 80 7F`。
  - **FireWater**：逗号分隔的 CSV 文本行。
  - **RawData**：原始字节流，按 ASCII 或 HEX 查看。
- 实时波形：深色示波器风格，图例、自动 Y 轴（手动缩放后关闭，双击恢复）、滚轮缩放 X/Y 轴、拖动、双击复位、暂停/恢复。
- 通道面板：显示/隐藏、颜色、当前值、最近样本的最小/最大值。
- 发送栏：ASCII / HEX 输入，支持 CR、LF、CRLF 行尾。
- 串口收发与解析在独立线程中运行，界面按批次刷新。
- 每通道环形缓冲最多保留 200,000 个样本。
- 检查更新：**帮助 → 检查更新** 查询 GitHub Releases（启动后每日也会自动检查一次）。安装版下载安装包、校验 SHA256 后静默启动安装程序；便携版打开发布页面。

## 协议说明

| 协议 | 格式 | 说明 |
|---|---|---|
| JustFloat | 小端 `float32` 数组 + `00 00 80 7F` | 帧尾是 `+Inf` 的字节表示；若某通道数据恰为 `+Inf`，会与帧尾冲突，属协议固有限制。 |
| FireWater | 每行 CSV，以 `\n` 结束 | 支持 `\r\n`；空行和非法数值行会被丢弃。 |
| RawData | 原始字节流 | 不做帧解析，仅按 ASCII/HEX 显示。 |

启动初期若通道数不一致会自动按后续完整帧校准（1–64 通道），以兼容中途打开串口；连续 4 帧一致后锁定通道数，之后通道数不匹配的帧会被整帧丢弃。

协议参考：[VOFA-Protocol-Driver](https://github.com/skjnsnb/VOFA-Protocol-Driver)（MIT）。本仓库未复制其源码。

## 从源码构建

前置条件：

- Windows 10/11 x64。
- Visual Studio 2026，MSVC v145 工具集，以及 [Qt VS Tools](https://marketplace.visualstudio.com/items?itemName=TheQtCompany.QtVSTools)。工程目标工具集为 `v145`，使用其他 Visual Studio 版本时请先重定向工具集。
- Qt 6.11.1 `msvc2022_64`，包含 `core`、`gui`、`widgets`、`serialport`、`printsupport`、`network` 模块。
- QCustomPlot 2.1.1 已随仓库提供，位于 `qcustomplot/`，无需另行下载。

工程是 Qt VS Tools 的 MSBuild 工程，不是 CMake 或 qmake；仅提供 x64 Debug / Release 配置。

构建前请修改 [Baud.vcxproj](Baud.vcxproj) 中的本机路径：

- 在 Qt VS Tools 中把本机 Qt 版本注册为 `qt6_msvc`，或修改工程里的 `<QtInstall>` 为实际注册名。
- post-build 的 `windeployqt` 命令指向 `D:\Software\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe`，Release 还会从 `$(VCInstallDir)Redist\MSVC\14.51.36231` 复制 VC 运行库。请按自己的 Qt 与 Visual Studio 安装位置同步修改。

在 Developer PowerShell 中构建：

```powershell
msbuild Baud.slnx /p:Configuration=Debug /p:Platform=x64 /m
msbuild Baud.slnx /p:Configuration=Release /p:Platform=x64 /m
```

产物：

- Debug：`x64\Debug\Baud.exe`
- Release：`x64\Release\` 整个目录。`windeployqt` 已部署 Qt DLL 与插件，可直接拷贝运行。

## 使用步骤

1. 连接串口设备，启动 Baud 并刷新串口列表。
2. 选择端口、波特率与串口参数，选择设备使用的协议，点击 **连接**。
3. 右侧区域会自动切换：JustFloat / FireWater 显示波形，RawData 显示原始数据监视器。
4. 波形操作：滚轮缩放 X/Y 轴、拖动平移、双击复位并恢复 Y 轴自动缩放；**暂停/继续** 控制刷新，**清空** 清除已采集数据。
5. 在发送栏选择 ASCII 或 HEX、设置行尾，输入内容后点击 **发送**。
6. **帮助 → 检查更新** 可手动检查 GitHub Releases；启动后每日也会自动检查一次。

## 回环测试

将 USB-TTL 的 TX 与 RX 短接，在发送栏发送以下数据验证：

- JustFloat（HEX）：`00 00 80 3F 00 00 00 40 00 00 80 7F` → 2 通道，值 1.0 / 2.0。
- FireWater（ASCII）：`1,2,3` + CRLF → 3 通道，值 1 / 2 / 3。
- RawData（ASCII）：`Hello` → 监视页显示 `Hello`。

## 目录结构

```text
Baud.cpp / Baud.h / main.cpp   主窗口与程序入口
src/protocol/                  协议类型、JustFloat / FireWater 解析器
src/serial/                    串口工作线程与参数
src/model/                     通道环形缓冲
src/ui/                        QCustomPlot 波形控件与主题
src/update/                    GitHub Releases 更新检查、下载与安装器启动
qcustomplot/                   QCustomPlot 2.1.1（GPLv3）
LICENSES/                      Qt LGPL-3.0 许可文本
```

## 许可证

本项目以 **GNU General Public License v3.0 or later（GPL-3.0-or-later）** 发布，完整许可文本见 [LICENSE](LICENSE)。

第三方组件声明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)：

- **QCustomPlot 2.1.1**：GPL-3.0-or-later，源码位于 `qcustomplot/`，许可文本见 `qcustomplot/GPL.txt`。
- **Qt 6.11.1**：LGPL-3.0-only / GPL-3.0-only（另有商业许可）。本项目动态链接 Qt，未修改 Qt 源码，LGPL 文本见 `LICENSES/LGPL-3.0.txt`。
- **VOFA-Protocol-Driver**：MIT，仅作协议参考。

分发二进制版本（如 `x64\Release` 目录）时，请同时附带 `LICENSE`、第三方许可文本与源码获取方式。源码发布于 https://github.com/AdonisZeng/Baud ，Qt 采用动态链接，用户可替换 Qt DLL。
