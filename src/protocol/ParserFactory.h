// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "IProtocolParser.h"

#include <memory>

// 按协议类型创建解析器；RawData 不需要解析器，返回 nullptr
std::unique_ptr<IProtocolParser> createParser(ProtocolMode mode);
