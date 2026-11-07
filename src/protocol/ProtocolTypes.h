// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include <QByteArray>
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
    QVector<double> values;
};

using FrameBatch = QVector<Frame>;

Q_DECLARE_METATYPE(Frame)
Q_DECLARE_METATYPE(FrameBatch)
Q_DECLARE_METATYPE(ProtocolMode)
