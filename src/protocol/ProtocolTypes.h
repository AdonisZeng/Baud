// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include <QMetaType>
#include <QVector>

// VOFA+ 的三种通信协议
enum class ProtocolMode {
    RawData = 0,   // 原始字节流，不做数值解析
    JustFloat = 1, // 小端 float32 数组 + 固定帧尾
    FireWater = 2  // CSV 文本行
};

// 一帧解析后的通道数据（统一用 double，兼容 FireWater 的文本数值）
struct Frame {
    // 该帧的时间轴坐标（秒）。由 SerialWorker 在串口线程内打点：
    // 串口是成批交付的，批内真实到达时刻不可知，因此以“批到达时刻”为锚，
    // 批内各帧按实测采样周期等距展开，既保证严格单调递增（绘图必需），
    // 又让相邻帧间隔等于真实周期。
    double t = 0.0;
    QVector<double> values;
};

using FrameBatch = QVector<Frame>;

Q_DECLARE_METATYPE(Frame)
Q_DECLARE_METATYPE(FrameBatch)
Q_DECLARE_METATYPE(ProtocolMode)
