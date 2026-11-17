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

} // namespace ValueFormat
