// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "FrameTimestamper.h"

#include <algorithm>
#include <cmath>

namespace {
// 取下四分位数：抗“个别批次偏大”的能力比均值强
double lowerQuartile(QVector<double> values)
{
    if (values.isEmpty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const int index = static_cast<int>((values.size() - 1) / 4);
    return values.at(index);
}
} // namespace

void FrameTimestamper::reset()
{
    m_prevChunkTime = 0.0;
    m_hasPrevChunk = false;
    m_lastFrameTime = 0.0;
    m_hasFrameTime = false;
    m_candidates.clear();
    m_period = 0.0;
    m_jitter = 0.0;
}

bool FrameTimestamper::onChunk(double chunkTime, int frameCount)
{
    const double prevChunkTime = m_prevChunkTime;
    const bool hasPrevChunk = m_hasPrevChunk;
    m_prevChunkTime = chunkTime;
    m_hasPrevChunk = true;

    if (frameCount <= 0)
        return false;

    if (m_period <= 0.0) {
        // 首批帧是一次性到达的，不能计入速率统计（它们是在窗口起点之前产生的），
        // 只用它来开启统计窗口。
        m_period = kInitialPeriod;
        m_candidates.clear();
        return true;
    }

    if (hasPrevChunk) {
        const double span = chunkTime - prevChunkTime;
        if (span > 0.0) {
            // 本批隐含的平均周期：到达时间差 / 本批帧数
            m_candidates.append(span / static_cast<double>(frameCount));
            if (m_candidates.size() > kCandidateWindow)
                m_candidates.remove(0, m_candidates.size() - kCandidateWindow);
        }
    }

    if (m_candidates.size() < 4)
        return false;

    // 下四分位数：免疫“空闲很久后突然来一批”造成的偏大值
    const double target = lowerQuartile(m_candidates);

    // 抖动：候选值相对平滑周期的偏离幅度
    double deviation = 0.0;
    for (double candidate : m_candidates)
        deviation += (candidate - m_period) * (candidate - m_period);
    deviation = std::sqrt(deviation / static_cast<double>(m_candidates.size()));

    m_jitter += kJitterAlpha * (deviation - m_jitter);
    m_period += kPeriodAlpha * (target - m_period);

    return true;
}

void FrameTimestamper::stamp(FrameBatch &frames, double chunkTime)
{
    if (frames.isEmpty())
        return;

    const double period = m_period > 0.0 ? m_period : kInitialPeriod;
    const int count = frames.size();

    // 以“批到达时刻”为锚，批内各帧按估计周期向前等距展开。
    // 这样既保证时间戳严格递增（绘图要求），又保证相邻帧间隔等于实测周期。
    double time = chunkTime;
    for (int i = count - 1; i >= 0; --i) {
        frames[i].t = time;
        time -= period;
    }

    // 兜底 1：若估计周期偏大导致批内时间倒流，逐帧修正为严格递增
    for (int i = 1; i < count; ++i) {
        if (frames.at(i).t <= frames.at(i - 1).t)
            frames[i].t = frames.at(i - 1).t + kMinPeriod;
    }

    // 兜底 2：本批首帧不得早于上一批末帧（跨批倒流会让图形数据失去有序性）。
    // 整批平移而不是压缩，保持批内原有的等间隔关系。
    if (m_hasFrameTime && frames.first().t <= m_lastFrameTime) {
        const double shift = m_lastFrameTime + kMinPeriod - frames.first().t;
        for (int i = 0; i < count; ++i)
            frames[i].t += shift;
    }

    m_lastFrameTime = frames.last().t;
    m_hasFrameTime = true;
}
