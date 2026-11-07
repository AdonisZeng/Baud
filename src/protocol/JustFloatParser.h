// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "IProtocolParser.h"

// JustFloat：小端 float32 数组，以 00 00 80 7F 作为帧尾
class JustFloatParser final : public IProtocolParser {
public:
    void reset() override;
    FrameBatch feed(const QByteArray &bytes) override;

private:
    QByteArray m_buffer;
};
