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

    // 自动估计采样周期与抖动时使用的窗口长度（帧）
    static constexpr int kDtWindow = 64;

    FrameBatch appendFrames(const FrameBatch &frames);
    void clear();

    int channelCount() const;
    qint64 sampleCount() const;
    qint64 firstStoredIndex() const;

    double valueAt(int channel, qint64 index) const;
    bool rangeMinMax(int channel, qint64 from, qint64 to,
                     double &minValue, double &maxValue) const;
    QVector<double> latestValues() const;

    // 第 index 帧的时间轴坐标（秒，相对本次会话起点）
    double timeAt(qint64 index) const;
    // 时间轴坐标 -> 帧序号（二分查找，插值到相邻两帧之间）
    qint64 indexAtTime(double time) const;
    // 时间轴坐标 -> 最接近的帧序号
    qint64 nearestIndexAtTime(double time) const;
    // 自动估计的采样周期（秒），数据不足时返回 0
    double estimatedPeriod() const;
    // 采样周期抖动（标准差，秒），数据不足时返回 0
    double periodJitter() const;

    // 批量读取 [from, to] 区间的数据用于导出。
    // times 长度为行数，values 按行主序展开（行 * 通道数 + 通道）。
    void readRange(qint64 from, qint64 to,
                   QVector<double> &times, QVector<double> &values) const;

private:
    void ensureBuffers();
    void resetTimeline();

    int m_channelCount = 0;
    int m_matchingChannelFrames = 0;
    qint64 m_sampleCount = 0;
    QVector<QVector<double>> m_data;
    // 与 m_data 同槽位的时间轴（秒，SerialWorker 侧以连接时刻为零点）
    QVector<double> m_times;
};
