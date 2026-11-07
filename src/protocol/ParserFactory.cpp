// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "ParserFactory.h"

#include "FireWaterParser.h"
#include "JustFloatParser.h"

std::unique_ptr<IProtocolParser> createParser(ProtocolMode mode)
{
    switch (mode) {
    case ProtocolMode::JustFloat:
        return std::make_unique<JustFloatParser>();
    case ProtocolMode::FireWater:
        return std::make_unique<FireWaterParser>();
    case ProtocolMode::RawData:
        break;
    }
    return nullptr;
}
