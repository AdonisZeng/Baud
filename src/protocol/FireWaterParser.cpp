// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "FireWaterParser.h"

#include <QString>

#include <cmath>

namespace {
constexpr int kLineLimit = 4 * 1024;
} // namespace

void FireWaterParser::reset()
{
    m_buffer.clear();
}

FrameBatch FireWaterParser::feed(const QByteArray &bytes)
{
    FrameBatch frames;
    m_buffer.append(bytes);

    while (true) {
        const int newlineIndex = m_buffer.indexOf('\n');
        if (newlineIndex < 0) {
            // 长时间没有换行，视为无效数据，避免缓冲无限增长
            if (m_buffer.size() > kLineLimit)
                m_buffer.clear();
            break;
        }

        QByteArray line = m_buffer.left(newlineIndex);
        m_buffer.remove(0, newlineIndex + 1);

        if (line.endsWith('\r'))
            line.chop(1);
        line = line.trimmed();

        if (line.isEmpty())
            continue;
        if (line.size() > kLineLimit)
            continue;

        const QStringList parts = QString::fromLatin1(line).split(QLatin1Char(','));
        Frame frame;
        frame.values.reserve(parts.size());

        bool valid = true;
        for (const QString &part : parts) {
            bool ok = false;
            const double value = part.trimmed().toDouble(&ok);
            // toDouble 会把 nan / inf 视为合法数字，它们不能进入绘图数据
            if (!ok || !std::isfinite(value)) {
                valid = false;
                break;
            }
            frame.values.append(value);
        }

        if (valid && !frame.values.isEmpty())
            frames.append(std::move(frame));
    }

    return frames;
}
