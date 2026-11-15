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
        m_times.clear();
        return;
    }

    m_data.resize(m_channelCount);
    for (auto &channel : m_data) {
        if (channel.size() != kCapacity)
            channel.resize(kCapacity);
    }
    if (m_times.size() != kCapacity)
        m_times.resize(kCapacity);
}

void ChannelStore::resetTimeline()
{
    m_data.clear();
    m_times.clear();
    m_sampleCount = 0;
}

FrameBatch ChannelStore::appendFrames(const FrameBatch &frames)
{
    FrameBatch accepted;

    for (const Frame &frame : frames) {
        const int count = frame.values.size();
        if (count <= 0 || count > kMaxChannels)
            continue;

        if (m_channelCount == 0) {
            m_channelCount = count;
            m_matchingChannelFrames = 0;
            ensureBuffers();
        } else if (count != m_channelCount) {
            if (m_matchingChannelFrames >= kChannelLockThreshold)
                continue;

            // 打开串口时可能正好处在一帧中间。通道数尚未稳定时，允许后续
            // 完整帧重新校准，避免首个残帧导致正确数据一直被整帧丢弃。
            m_channelCount = count;
            m_matchingChannelFrames = 0;
            resetTimeline();
            accepted.clear();
            ensureBuffers();
        }

        if (m_matchingChannelFrames < kChannelLockThreshold)
            ++m_matchingChannelFrames;

        const qint64 slot = m_sampleCount % kCapacity;
        for (int channel = 0; channel < m_channelCount; ++channel)
            m_data[channel][slot] = frame.values.at(channel);
        m_times[slot] = frame.t;

        ++m_sampleCount;
        accepted.append(frame);
    }

    return accepted;
}

void ChannelStore::clear()
{
    m_channelCount = 0;
    m_matchingChannelFrames = 0;
    m_sampleCount = 0;
    m_data.clear();
    m_times.clear();
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

double ChannelStore::timeAt(qint64 index) const
{
    if (index < firstStoredIndex() || index >= m_sampleCount)
        return std::numeric_limits<double>::quiet_NaN();
    if (m_times.isEmpty())
        return std::numeric_limits<double>::quiet_NaN();

    return m_times.at(index % kCapacity);
}

qint64 ChannelStore::indexAtTime(double time) const
{
    if (m_sampleCount <= 0 || m_times.isEmpty())
        return -1;

    const qint64 first = firstStoredIndex();
    const qint64 last = m_sampleCount - 1;

    // 时间戳严格单调递增，可直接二分查找
    qint64 low = first;
    qint64 high = last;
    while (low < high) {
        const qint64 mid = low + (high - low + 1) / 2;
        if (timeAt(mid) <= time)
            low = mid;
        else
            high = mid - 1;
    }
    return low;
}

qint64 ChannelStore::nearestIndexAtTime(double time) const
{
    const qint64 index = indexAtTime(time);
    if (index < 0)
        return -1;

    const qint64 next = index + 1;
    if (next >= m_sampleCount)
        return index;

    const double current = timeAt(index);
    const double following = timeAt(next);
    if (!std::isfinite(current) || !std::isfinite(following))
        return index;

    return (time - current <= following - time) ? index : next;
}

double ChannelStore::estimatedPeriod() const
{
    if (m_sampleCount < 2 || m_times.isEmpty())
        return 0.0;

    const qint64 last = m_sampleCount - 1;
    const qint64 first = std::max(firstStoredIndex(), last - kDtWindow);
    if (last <= first)
        return 0.0;

    const double from = timeAt(first);
    const double to = timeAt(last);
    if (!std::isfinite(from) || !std::isfinite(to))
        return 0.0;

    return (to - from) / static_cast<double>(last - first);
}

double ChannelStore::periodJitter() const
{
    if (m_sampleCount < 3 || m_times.isEmpty())
        return 0.0;

    const qint64 last = m_sampleCount - 1;
    const qint64 first = std::max(firstStoredIndex(), last - kDtWindow);
    if (last - first < 2)
        return 0.0;

    double sum = 0.0;
    double sumSquares = 0.0;
    int used = 0;
    double previous = timeAt(first);
    for (qint64 index = first + 1; index <= last; ++index) {
        const double current = timeAt(index);
        if (!std::isfinite(previous) || !std::isfinite(current))
            continue;
        const double delta = current - previous;
        sum += delta;
        sumSquares += delta * delta;
        ++used;
        previous = current;
    }
    if (used < 2)
        return 0.0;

    const double mean = sum / used;
    const double variance = std::max(0.0, sumSquares / used - mean * mean);
    return std::sqrt(variance);
}

void ChannelStore::readRange(qint64 from, qint64 to,
                            QVector<double> &times, QVector<double> &values) const
{
    times.clear();
    values.clear();

    if (m_channelCount <= 0 || m_times.isEmpty())
        return;

    from = std::max(from, firstStoredIndex());
    to = std::min(to, m_sampleCount - 1);
    if (from > to)
        return;

    const int rows = static_cast<int>(to - from + 1);
    times.reserve(rows);
    values.reserve(static_cast<qsizetype>(rows) * m_channelCount);

    for (qint64 index = from; index <= to; ++index) {
        times.append(m_times.at(index % kCapacity));
        for (int channel = 0; channel < m_channelCount; ++channel)
            values.append(m_data.at(channel).at(index % kCapacity));
    }
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
