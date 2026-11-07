// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include <QColor>

class QApplication;

// 固定深色主题与通道配色
namespace Theme {
void apply(QApplication &app);
QColor channelColor(int index);
} // namespace Theme
