// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "ProtocolTypes.h"

// 协议解析器接口：接收任意长度的字节块，返回本次能完整解析出的帧
class IProtocolParser {
public:
    virtual ~IProtocolParser() = default;

    virtual void reset() = 0;
    virtual FrameBatch feed(const QByteArray &bytes) = 0;
};
