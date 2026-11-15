// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "protocol/ProtocolTypes.h"

#include <QVector>
#include <QtGlobal>

// 帧时间戳与采样周期估计器。
//
// 背景：串口是“成批交付”的——驱动一次 readAll 可能带回几十毫秒内积压的全部字节，
// 批内各帧的精确到达时刻在 PC 侧不可知。本类据此重建时间轴：
//
//   1. 以“批到达时刻”为锚，批内各帧按实测周期向前等距展开；
//   2. 保证全序列时间戳严格递增（QCustomPlot 要求 X 坐标唯一且有序）；
//   3. 自动估计采样周期，全程无需用户填写 dt。
//
// 周期估计的关键在于“每批隐含的周期”这一观测量：
// 相邻两批的到达时间差除以本批帧数，就是该批隐含的平均周期。
// 这个量只会在“设备空闲导致批被推迟”时偏大，其余情况都准确，
// 因此取最近若干批候选值的下四分位数（而不是均值），
// 就能同时免疫“空闲后突发”和“批大小波动”，且对真实变速也能跟上。
class FrameTimestamper {
public:
    // 采样周期 EMA 系数
    static constexpr double kPeriodAlpha = 0.15;
    // 抖动 EMA 系数
    static constexpr double kJitterAlpha = 0.15;
    // 参与统计的最近批次数
    static constexpr int kCandidateWindow = 16;
    // 首批数据尚无参照周期时的初始假定
    static constexpr double kInitialPeriod = 0.001;
    // 时间戳最小间隔，防止相邻帧出现完全相同的 key
    static constexpr double kMinPeriod = 1e-9;

    // 复位（重新连接时调用）
    void reset();

    // 处理一批新到达的数据：chunkTime 为该批的到达时刻（秒），
    // frameCount 为该批解析出的帧数。返回 true 表示周期/抖动有更新。
    bool onChunk(double chunkTime, int frameCount);

    // 给本批帧打时间戳（必须在 onChunk 之后调用）
    void stamp(FrameBatch &frames, double chunkTime);

    double period() const { return m_period; }
    double jitter() const { return m_jitter; }

private:
    // 上一批到达时刻
    double m_prevChunkTime = 0.0;
    bool m_hasPrevChunk = false;
    // 上一帧的时间戳，用于保证跨批单调
    double m_lastFrameTime = 0.0;
    bool m_hasFrameTime = false;
    // 最近若干批各自隐含的周期候选值（秒）
    QVector<double> m_candidates;
    // 估计的采样周期与抖动（秒）
    double m_period = 0.0;
    double m_jitter = 0.0;
};
