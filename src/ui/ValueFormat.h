// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include <QString>

#include <algorithm>
#include <cmath>

// 数值显示格式化：通道表、测量面板、悬停标签共用同一套规则。
//
// 为什么不用 QString::number(value, 'g', n)：
// 'g' 在有效位用尽时立刻切科学计数，'g',4 意味着 10000 显示成 "1e+04"、
// 1234.5 被舍成 "1235"，观测波形时很难读。这里改为
//   |v| ∈ [1e-4, 1e7) → 定点显示，保留至多 significantDigits 位有效数字（去掉尾随 0）
//   其余量级        → 才退化成科学计数（极小/极大值仍有 6 位有效数字）
// 定点区间上限取 1e7 是因为通道表最小/最大列只有 58 px，约 8 个字符。
namespace ValueFormat {

inline QString format(double value, int significantDigits = 6)
{
    if (!std::isfinite(value))
        return QStringLiteral("--");
    if (value == 0.0)
        return QStringLiteral("0");

    const double magnitude = std::fabs(value);
    if (magnitude < 1e-4 || magnitude >= 1e7)
        return QString::number(value, 'g', significantDigits);

    const int exponent = static_cast<int>(std::floor(std::log10(magnitude)));
    const int decimals = std::clamp(significantDigits - 1 - exponent, 0, 10);
    QString text = QString::number(value, 'f', decimals);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }
    return text;
}

// 完整精度（12 位有效数字），用于 tooltip 等空间不受限制的地方
inline QString formatExact(double value)
{
    return std::isfinite(value) ? QString::number(value, 'g', 12) : QStringLiteral("--");
}

// 无损往返格式：输出能被 toDouble() 精确还原成同一个 double 的文本，
// 同时尽量短、尽量不用科学计数。导出 CSV / Markdown 与码值编辑都用它。
//
// 为什么不用 QString::number(value, 'g', 17)：
// 17 位有效数字虽然能无损还原，但 3.3 会写成 "3.2999999999999998"，
// 导出文件很难读；码值编辑器里更不能把用户填的数改写掉。
// 这里按“定点 → 科学计数”的顺序找最短的可还原写法，
// 定点分支会去掉尾随 0，因此 3.3 -> "3.3"、0.001 -> "0.001"。
inline QString roundTrip(double value)
{
    if (std::isnan(value))
        return QStringLiteral("nan");
    if (std::isinf(value))
        return value > 0.0 ? QStringLiteral("inf") : QStringLiteral("-inf");
    if (value == 0.0)
        return QStringLiteral("0");

    const double magnitude = std::fabs(value);
    // 量级适中的用定点（去掉尾随 0），过小/过大才退回科学计数
    if (magnitude >= 1e-4 && magnitude < 1e15) {
        for (int decimals = 0; decimals <= 17; ++decimals) {
            QString text = QString::number(value, 'f', decimals);
            if (text.contains(QLatin1Char('.'))) {
                while (text.endsWith(QLatin1Char('0')))
                    text.chop(1);
                if (text.endsWith(QLatin1Char('.')))
                    text.chop(1);
            }
            bool ok = false;
            const double parsed = text.toDouble(&ok);
            if (ok && parsed == value)
                return text;
        }
    }

    for (int precision = 1; precision <= 17; ++precision) {
        const QString text = QString::number(value, 'g', precision);
        bool ok = false;
        const double parsed = text.toDouble(&ok);
        if (ok && parsed == value)
            return text;
    }
    return QString::number(value, 'g', 17);
}

} // namespace ValueFormat
