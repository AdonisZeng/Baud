// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "JustFloatParser.h"

#include <QtEndian>

#include <array>
#include <cstring>

namespace {
// 0x7F800000（+Inf）的小端字节序，VOFA+ 固定帧尾
constexpr std::array<uchar, 4> kJustFloatTail = {0x00, 0x00, 0x80, 0x7F};
constexpr int kBufferLimit = 64 * 1024;
} // namespace

void JustFloatParser::reset()
{
    m_buffer.clear();
}

FrameBatch JustFloatParser::feed(const QByteArray &bytes)
{
    FrameBatch frames;
    m_buffer.append(bytes);

    static const QByteArray tail(reinterpret_cast<const char *>(kJustFloatTail.data()),
                                 kJustFloatTail.size());

    while (true) {
        const int tailIndex = m_buffer.indexOf(tail);
        if (tailIndex < 0) {
            // 没有完整帧尾：只保留末尾最多 3 字节，避免垃圾数据无限增长
            if (m_buffer.size() > kBufferLimit)
                m_buffer = m_buffer.right(kJustFloatTail.size() - 1);
            break;
        }

        if (tailIndex == 0) {
            // 空帧，直接丢弃帧尾
            m_buffer.remove(0, kJustFloatTail.size());
            continue;
        }

        if (tailIndex % static_cast<int>(sizeof(float)) != 0) {
            // 数据区长度不是 4 的倍数：只丢弃 1 字节后重新查找帧尾，
            // 避免单个噪声字节导致后续整帧一起丢失
            m_buffer.remove(0, 1);
            continue;
        }

        const int channelCount = tailIndex / static_cast<int>(sizeof(float));
        Frame frame;
        frame.values.reserve(channelCount);
        const uchar *raw = reinterpret_cast<const uchar *>(m_buffer.constData());
        for (int i = 0; i < channelCount; ++i) {
            const quint32 bits = qFromLittleEndian<quint32>(raw + i * sizeof(float));
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(float));
            frame.values.append(static_cast<double>(value));
        }
        frames.append(std::move(frame));
        m_buffer.remove(0, tailIndex + kJustFloatTail.size());
    }

    return frames;
}
