// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "IProtocolParser.h"

// FireWater：CSV 文本行，每行以 \n 结束，字段用逗号分隔
class FireWaterParser final : public IProtocolParser {
public:
    void reset() override;
    FrameBatch feed(const QByteArray &bytes) override;

private:
    QByteArray m_buffer;
};
