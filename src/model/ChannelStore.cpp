// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "ChannelStore.h"

#include <algorithm>
#include <cmath>
#include <limits>

void ChannelStore::ensureBuffers()
{
    if (m_channelCount <= 0) {
        m_data.clear();
        return;
    }

    m_data.resize(m_channelCount);
    for (auto &channel : m_data) {
        if (channel.size() != kCapacity)
            channel.resize(kCapacity);
    }
}

FrameBatch ChannelStore::appendFrames(const FrameBatch &frames)
{
    FrameBatch accepted;

    if (m_channelCount == 0) {
        // 首帧决定通道数；忽略噪声帧和超限帧
        for (const Frame &frame : frames) {
            const int count = frame.values.size();
            if (count > 0 && count <= kMaxChannels) {
                m_channelCount = count;
                ensureBuffers();
                break;
            }
        }
        if (m_channelCount == 0)
            return accepted;
    }

    for (const Frame &frame : frames) {
        if (frame.values.size() != m_channelCount)
            continue;

        const qint64 slot = m_sampleCount % kCapacity;
        for (int channel = 0; channel < m_channelCount; ++channel)
            m_data[channel][slot] = frame.values.at(channel);

        ++m_sampleCount;
        accepted.append(frame);
    }

    return accepted;
}

void ChannelStore::clear()
{
    m_channelCount = 0;
    m_sampleCount = 0;
    m_data.clear();
}

int ChannelStore::channelCount() const
{
    return m_channelCount;
}

qint64 ChannelStore::sampleCount() const
{
    return m_sampleCount;
}

qint64 ChannelStore::firstStoredIndex() const
{
    return std::max<qint64>(0, m_sampleCount - kCapacity);
}

double ChannelStore::valueAt(int channel, qint64 index) const
{
    if (channel < 0 || channel >= m_channelCount)
        return std::numeric_limits<double>::quiet_NaN();
    if (index < firstStoredIndex() || index >= m_sampleCount)
        return std::numeric_limits<double>::quiet_NaN();

    return m_data.at(channel).at(index % kCapacity);
}

bool ChannelStore::rangeMinMax(int channel, qint64 from, qint64 to,
                               double &minValue, double &maxValue) const
{
    if (channel < 0 || channel >= m_channelCount || m_sampleCount <= 0)
        return false;

    from = std::max(from, firstStoredIndex());
    to = std::min(to, m_sampleCount - 1);
    if (from > to)
        return false;

    bool found = false;
    const auto &data = m_data.at(channel);
    for (qint64 index = from; index <= to; ++index) {
        const double value = data.at(index % kCapacity);
        if (!std::isfinite(value))
            continue;
        if (!found) {
            minValue = maxValue = value;
            found = true;
        } else {
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }
    }
    return found;
}

QVector<double> ChannelStore::latestValues() const
{
    QVector<double> values;
    if (m_channelCount <= 0 || m_sampleCount <= 0)
        return values;

    values.reserve(m_channelCount);
    const qint64 latest = m_sampleCount - 1;
    for (int channel = 0; channel < m_channelCount; ++channel)
        values.append(valueAt(channel, latest));
    return values;
}
