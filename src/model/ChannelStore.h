// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "protocol/ProtocolTypes.h"

#include <QVector>

// GUI 线程内的通道环形缓冲；启动初期自动校准通道数，稳定后锁定
class ChannelStore {
public:
    static constexpr int kMaxChannels = 64;
    static constexpr int kChannelLockThreshold = 4;
    static constexpr qint64 kCapacity = 200000;

    FrameBatch appendFrames(const FrameBatch &frames);
    void clear();

    int channelCount() const;
    qint64 sampleCount() const;
    qint64 firstStoredIndex() const;

    double valueAt(int channel, qint64 index) const;
    bool rangeMinMax(int channel, qint64 from, qint64 to,
                     double &minValue, double &maxValue) const;
    QVector<double> latestValues() const;

private:
    void ensureBuffers();

    int m_channelCount = 0;
    int m_matchingChannelFrames = 0;
    qint64 m_sampleCount = 0;
    QVector<QVector<double>> m_data;
};
