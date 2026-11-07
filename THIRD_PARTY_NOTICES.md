# Third-Party Notices / 第三方组件声明

本项目（Baud）以 GPL-3.0-or-later 发布，完整许可文本见根目录 [LICENSE](LICENSE)。

## QCustomPlot 2.1.1

- 版权：Copyright (C) 2011-2022 Emanuel Eichhammer
- 许可证：GNU General Public License v3.0 or later（GPL-3.0-or-later）
- 仓库中的文件：`qcustomplot/qcustomplot.h`、`qcustomplot/qcustomplot.cpp`
- 许可文本：`qcustomplot/GPL.txt`
- 上游：https://www.qcustomplot.com/

QCustomPlot 以源码形式编译进本程序，因此本项目整体按 GPL-3.0-or-later 分发。分发二进制时，需要同时提供对应源码（本仓库即为源码）并保留 QCustomPlot 的版权头与许可文本。

## Qt 6.11.1

- 版权：Copyright (C) The Qt Company Ltd. and Qt contributors
- 许可证：LGPL-3.0-only / GPL-3.0-only（Qt 亦提供商业许可）
- 使用方式：本项目以动态链接方式使用 Qt，不修改 Qt 源码，不静态链接 Qt。
- 许可文本：`LICENSES/LGPL-3.0.txt`（GPL-3.0 文本见根目录 `LICENSE`）。
- 上游许可说明：https://www.qt.io/licensing/open-source-lgpl-obligations

分发包含 Qt DLL 的二进制版本时，请随附 Qt 的 LGPL-3.0 / GPL-3.0 许可文本与版权声明，并保持动态链接，使用户能够替换 Qt 库。`x64\Release` 目录中的 `Qt6*.dll` 不应作为源码仓库的一部分提交。

## VOFA-Protocol-Driver

- 版权：Copyright (c) 2021 Jelin
- 许可证：MIT License
- 上游：https://github.com/skjnsnb/VOFA-Protocol-Driver

该仓库仅作为 VOFA+ 协议格式参考，本项目未复制其源代码。

## 许可证兼容性说明

GPL-3.0-or-later 与 MIT 许可兼容；Qt 的 LGPL-3.0-only / GPL-3.0-only 均可用于 GPLv3 项目。当前组合不存在授权冲突。

如果未来希望将本项目改为 MIT、Apache-2.0 或闭源发布，必须：

1. 购买 QCustomPlot 商业许可，或移除 QCustomPlot 并替换为授权兼容的绘图实现；
2. 重新检查 Qt 的链接方式与 LGPL 义务，或购买 Qt 商业许可。
