// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include <QColor>

class QApplication;

// 深色 / 浅色主题与通道配色。
// 颜色令牌同时供全局样式表（QSS）与 QCustomPlot 使用，
// 切换主题时两边必须一起刷新，否则会出现黑底黑字。
namespace Theme {

enum class Mode { Dark, Light };

struct Colors {
    // 控件
    QColor window;
    QColor panel;
    QColor base;
    QColor baseDisabled;
    QColor alternateBase;
    QColor monitorBackground;
    QColor text;
    QColor textMuted;
    QColor textDisabled;
    QColor headerText;
    QColor border;
    QColor borderControl;
    QColor borderHover;
    QColor button;
    QColor buttonHover;
    QColor buttonPressed;
    QColor buttonDisabled;
    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor accentDisabled;
    QColor scrollHandle;
    QColor scrollHandleHover;
    QColor toolTipBackground;
    QColor error;

    // 波形区
    QColor plotCanvas;
    QColor plotArea;
    QColor gridLine;
    QColor zeroLine;
    QColor axisLine;
    QColor axisTick;
    QColor tickText;
    QColor axisLabel;
    QColor legendBackground;
    QColor legendBorder;
    QColor hint;
    QColor hover;
    QColor badgeText;
    QColor badgeFill;
    QColor valueLabelFill;
    QColor markerDefault;
    QColor markerStart;
    QColor markerEnd;
    QColor measureHighlight;
};

Mode mode();
void setMode(Mode mode);
const Colors &colors();

// 切换模式并应用 palette + 样式表
void apply(QApplication &app, Mode mode);

// 主题持久化（QSettings 的 ui/theme）
Mode loadMode();
void saveMode(Mode mode);

QColor channelColor(int index);

} // namespace Theme
