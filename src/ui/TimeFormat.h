// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include <QString>

#include <cmath>

// 时间显示格式化：统一用毫秒 / 秒 / 分 三档，单位随量级自动切换
namespace TimeFormat {

// 形如 "12.345 s" / "865.0 ms" / "1.234 min"
inline QString format(double seconds)
{
    if (!std::isfinite(seconds))
        return QStringLiteral("--");

    const double abs = std::fabs(seconds);
    if (abs < 1.0) {
        const double ms = seconds * 1000.0;
        return QString::number(ms, 'f', abs < 0.01 ? 3 : (abs < 0.1 ? 2 : 1))
               + QStringLiteral(" ms");
    }
    if (abs < 60.0) {
        return QString::number(seconds, 'f', abs < 10.0 ? 3 : 2)
               + QStringLiteral(" s");
    }
    return QString::number(seconds / 60.0, 'f', 2) + QStringLiteral(" min");
}

// 形如 "+865.0 ms"，用于差值展示
inline QString formatDelta(double seconds)
{
    if (!std::isfinite(seconds))
        return QStringLiteral("--");
    const QString text = format(std::fabs(seconds));
    return seconds < 0 ? QStringLiteral("-") + text : QStringLiteral("+") + text;
}

// 紧凑格式，用于悬浮气泡等空间受限处
inline QString formatCompact(double seconds)
{
    if (!std::isfinite(seconds))
        return QStringLiteral("--");
    const double abs = std::fabs(seconds);
    if (abs < 1.0)
        return QString::number(seconds * 1000.0, 'f', abs < 0.01 ? 2 : 1)
               + QStringLiteral("ms");
    if (abs < 60.0)
        return QString::number(seconds, 'f', abs < 10.0 ? 2 : 1) + QStringLiteral("s");
    return QString::number(seconds / 60.0, 'f', 1) + QStringLiteral("min");
}

} // namespace TimeFormat
