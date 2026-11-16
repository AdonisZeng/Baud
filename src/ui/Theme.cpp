// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QVector>

namespace {

const QColor kDarkChannelColors[] = {
    QColor("#4C9AFF"), QColor("#FF8A3D"), QColor("#4CD964"), QColor("#B07CFF"),
    QColor("#2DD4BF"), QColor("#FFD166"), QColor("#FF5D8F"), QColor("#A0AEC0"),
};

const int kChannelColorCount =
    static_cast<int>(sizeof(kDarkChannelColors) / sizeof(kDarkChannelColors[0]));

// 浅色背景上要压深，否则亮黄、青色几乎看不见
const QColor kLightChannelColors[] = {
    QColor("#1F6FEB"), QColor("#D9480F"), QColor("#2B8A3E"), QColor("#7048E8"),
    QColor("#0B7285"), QColor("#B26B00"), QColor("#C2255C"), QColor("#495057"),
};

Theme::Mode g_mode = Theme::Mode::Dark;

Theme::Colors makeDarkColors()
{
    Theme::Colors c;
    c.window = QColor("#1B1F24");
    c.panel = QColor("#20262D");
    c.base = QColor("#171C22");
    c.baseDisabled = QColor("#1B2027");
    c.alternateBase = QColor("#1B2128");
    c.monitorBackground = QColor("#10151A");
    c.text = QColor("#E6EAF0");
    c.textMuted = QColor("#C8D2DC");
    c.textDisabled = QColor("#6F7B87");
    c.headerText = QColor("#A9B4C0");
    c.border = QColor("#2A313A");
    c.borderControl = QColor("#343C47");
    c.borderHover = QColor("#46515E");
    c.button = QColor("#2A313A");
    c.buttonHover = QColor("#333B46");
    c.buttonPressed = QColor("#232A32");
    c.buttonDisabled = QColor("#242A31");
    c.accent = QColor("#2F6FED");
    c.accentHover = QColor("#4C84F5");
    c.accentPressed = QColor("#2457C7");
    c.accentDisabled = QColor("#2A3448");
    c.scrollHandle = QColor("#3A434F");
    c.scrollHandleHover = QColor("#4B5664");
    c.toolTipBackground = QColor("#242A31");
    c.error = QColor("#FF6B6B");

    c.plotCanvas = QColor("#0F1419");
    c.plotArea = QColor("#121820");
    c.gridLine = QColor(255, 255, 255, 28);
    c.zeroLine = QColor(255, 255, 255, 60);
    c.axisLine = QColor(255, 255, 255, 70);
    c.axisTick = QColor(255, 255, 255, 50);
    c.tickText = QColor("#A9B4C0");
    c.axisLabel = QColor("#C8D2DC");
    c.legendBackground = QColor(18, 24, 32, 210);
    c.legendBorder = QColor(255, 255, 255, 45);
    c.hint = QColor("#7E8DA0");
    c.hover = QColor("#9AA7B4");
    c.badgeText = QColor("#EDF2F7");
    c.badgeFill = QColor(15, 20, 25, 235);
    c.valueLabelFill = QColor(15, 20, 25, 210);
    c.markerDefault = QColor("#E6EDF5");
    c.markerStart = QColor("#FF4D5A");
    c.markerEnd = QColor("#D66BFF");
    c.measureHighlight = QColor("#F5C451");
    return c;
}

Theme::Colors makeLightColors()
{
    Theme::Colors c;
    c.window = QColor("#F3F5F8");
    c.panel = QColor("#FFFFFF");
    c.base = QColor("#FFFFFF");
    c.baseDisabled = QColor("#EFF2F5");
    c.alternateBase = QColor("#F7F9FB");
    c.monitorBackground = QColor("#FBFCFD");
    c.text = QColor("#1F2933");
    c.textMuted = QColor("#4A5560");
    c.textDisabled = QColor("#9AA5B1");
    c.headerText = QColor("#5B6773");
    c.border = QColor("#DCE3EA");
    c.borderControl = QColor("#C8D2DC");
    c.borderHover = QColor("#9DB0C3");
    c.button = QColor("#E9EEF4");
    c.buttonHover = QColor("#DDE6EF");
    c.buttonPressed = QColor("#D2DCE6");
    c.buttonDisabled = QColor("#EDF1F5");
    c.accent = QColor("#2F6FED");
    c.accentHover = QColor("#4C84F5");
    c.accentPressed = QColor("#2457C7");
    c.accentDisabled = QColor("#C9D6F5");
    c.scrollHandle = QColor("#C3CCD7");
    c.scrollHandleHover = QColor("#A9B6C4");
    c.toolTipBackground = QColor("#FFFFFF");
    c.error = QColor("#C92A2A");

    c.plotCanvas = QColor("#FFFFFF");
    c.plotArea = QColor("#FBFCFE");
    c.gridLine = QColor(0, 0, 0, 30);
    c.zeroLine = QColor(0, 0, 0, 70);
    c.axisLine = QColor(0, 0, 0, 90);
    c.axisTick = QColor(0, 0, 0, 70);
    c.tickText = QColor("#5B6773");
    c.axisLabel = QColor("#3E4A56");
    c.legendBackground = QColor(255, 255, 255, 215);
    c.legendBorder = QColor(0, 0, 0, 45);
    c.hint = QColor("#9AA5B1");
    c.hover = QColor("#7B8794");
    c.badgeText = QColor("#1F2933");
    c.badgeFill = QColor(255, 255, 255, 238);
    c.valueLabelFill = QColor(255, 255, 255, 220);
    c.markerDefault = QColor("#495057");
    c.markerStart = QColor("#E03131");
    c.markerEnd = QColor("#9C36B5");
    c.measureHighlight = QColor("#B26B00");
    return c;
}

Theme::Colors &currentColors()
{
    static Theme::Colors colors = makeDarkColors();
    return colors;
}

QPalette buildPalette(const Theme::Colors &c)
{
    QPalette palette;
    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.base);
    palette.setColor(QPalette::AlternateBase, c.alternateBase);
    palette.setColor(QPalette::ToolTipBase, c.toolTipBackground);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, c.button);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::BrightText, c.error);
    palette.setColor(QPalette::Link, c.accent);
    palette.setColor(QPalette::Highlight, c.accent);
    palette.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    palette.setColor(QPalette::Disabled, QPalette::Text, c.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, c.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, c.textDisabled);
    return palette;
}

QString buildStyleSheet(const Theme::Colors &c)
{
    static const char *kTemplate = R"QSS(QWidget {
    font-family: "Microsoft YaHei UI";
    font-size: 9pt;
}
QMainWindow {
    background: @{window};
}
QGroupBox {
    background: @{panel};
    border: 1px solid @{border};
    border-radius: 6px;
    margin-top: 14px;
    padding-top: 6px;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 10px;
    padding: 0 4px;
    color: @{textMuted};
}
QLabel {
    color: @{textMuted};
}
QPushButton {
    min-height: 28px;
    padding: 0 12px;
    border: 1px solid @{borderControl};
    border-radius: 6px;
    background: @{button};
    color: @{text};
}
QPushButton:hover {
    background: @{buttonHover};
    border-color: @{borderHover};
}
QPushButton:pressed {
    background: @{buttonPressed};
}
QPushButton:disabled {
    background: @{buttonDisabled};
    color: @{textDisabled};
    border-color: @{border};
}
QPushButton#primaryButton {
    background: @{accent};
    border-color: @{accent};
    color: #FFFFFF;
    font-weight: 600;
}
QPushButton#primaryButton:hover {
    background: @{accentHover};
    border-color: @{accentHover};
}
QPushButton#primaryButton:pressed {
    background: @{accentPressed};
    border-color: @{accentPressed};
}
QPushButton#primaryButton:disabled {
    background: @{accentDisabled};
    border-color: @{accentDisabled};
    color: @{textDisabled};
}
/* 菜单栏角落的主题按钮：压扁高度，避免把菜单栏撑高 */
QPushButton#themeToggle {
    min-height: 20px;
    max-height: 22px;
    padding: 0 10px;
    font-size: 8pt;
}
QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
    min-height: 26px;
    padding: 0 8px;
    border: 1px solid @{borderControl};
    border-radius: 6px;
    background: @{base};
    color: @{text};
    selection-background-color: @{accent};
}
QComboBox:hover, QLineEdit:hover {
    border-color: @{borderHover};
}
QComboBox:disabled, QLineEdit:disabled {
    color: @{textDisabled};
    background: @{baseDisabled};
}
QComboBox::drop-down {
    width: 22px;
    border: none;
}
QComboBox QAbstractItemView {
    background: @{panel};
    border: 1px solid @{borderControl};
    selection-background-color: @{accent};
    outline: none;
}
QCheckBox {
    color: @{textMuted};
    spacing: 6px;
}
QTableWidget {
    background: @{base};
    alternate-background-color: @{alternateBase};
    border: 1px solid @{border};
    border-radius: 6px;
    gridline-color: @{border};
    color: @{text};
    selection-background-color: @{accent};
}
QHeaderView::section {
    background: @{panel};
    color: @{headerText};
    border: none;
    border-bottom: 1px solid @{border};
    padding: 4px 6px;
    font-weight: 600;
}
QPlainTextEdit {
    background: @{monitorBackground};
    color: @{textMuted};
    font-family: "Consolas";
    font-size: 9pt;
    border: 1px solid @{border};
    border-radius: 6px;
    selection-background-color: @{accent};
}
QTextBrowser {
    background: @{base};
    color: @{textMuted};
    border: 1px solid @{border};
    border-radius: 6px;
}
QProgressBar {
    background: @{base};
    border: 1px solid @{borderControl};
    border-radius: 6px;
    min-height: 18px;
    text-align: center;
    color: @{textMuted};
}
QProgressBar::chunk {
    background: @{accent};
    border-radius: 5px;
}
QScrollBar:vertical {
    background: @{base};
    width: 10px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: @{scrollHandle};
    min-height: 24px;
    border-radius: 5px;
}
QScrollBar::handle:vertical:hover {
    background: @{scrollHandleHover};
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar:horizontal {
    background: @{base};
    height: 10px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: @{scrollHandle};
    min-width: 24px;
    border-radius: 5px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}
QStatusBar {
    background: @{base};
    color: @{headerText};
    border-top: 1px solid @{border};
}
QMenuBar {
    background: @{window};
    color: @{textMuted};
}
QMenuBar::item:selected {
    background: @{button};
}
QMenu {
    background: @{panel};
    color: @{text};
    border: 1px solid @{borderControl};
}
QMenu::item:selected {
    background: @{accent};
}
QToolTip {
    background: @{toolTipBackground};
    color: @{text};
    border: 1px solid @{borderControl};
})QSS";

    QString sheet = QString::fromUtf8(kTemplate);
    const QVector<QPair<QString, QColor>> tokens = {
        {QStringLiteral("@{window}"), c.window},
        {QStringLiteral("@{panel}"), c.panel},
        {QStringLiteral("@{base}"), c.base},
        {QStringLiteral("@{baseDisabled}"), c.baseDisabled},
        {QStringLiteral("@{alternateBase}"), c.alternateBase},
        {QStringLiteral("@{monitorBackground}"), c.monitorBackground},
        {QStringLiteral("@{text}"), c.text},
        {QStringLiteral("@{textMuted}"), c.textMuted},
        {QStringLiteral("@{textDisabled}"), c.textDisabled},
        {QStringLiteral("@{headerText}"), c.headerText},
        {QStringLiteral("@{border}"), c.border},
        {QStringLiteral("@{borderControl}"), c.borderControl},
        {QStringLiteral("@{borderHover}"), c.borderHover},
        {QStringLiteral("@{button}"), c.button},
        {QStringLiteral("@{buttonHover}"), c.buttonHover},
        {QStringLiteral("@{buttonPressed}"), c.buttonPressed},
        {QStringLiteral("@{buttonDisabled}"), c.buttonDisabled},
        {QStringLiteral("@{accent}"), c.accent},
        {QStringLiteral("@{accentHover}"), c.accentHover},
        {QStringLiteral("@{accentPressed}"), c.accentPressed},
        {QStringLiteral("@{accentDisabled}"), c.accentDisabled},
        {QStringLiteral("@{scrollHandle}"), c.scrollHandle},
        {QStringLiteral("@{scrollHandleHover}"), c.scrollHandleHover},
        {QStringLiteral("@{toolTipBackground}"), c.toolTipBackground},
    };
    for (const auto &token : tokens)
        sheet.replace(token.first, token.second.name());
    return sheet;
}

} // namespace

Theme::Mode Theme::mode()
{
    return g_mode;
}

void Theme::setMode(Mode mode)
{
    g_mode = mode;
    currentColors() = mode == Mode::Light ? makeLightColors() : makeDarkColors();
}

const Theme::Colors &Theme::colors()
{
    return currentColors();
}

void Theme::apply(QApplication &app, Mode mode)
{
    setMode(mode);
    const Colors &c = colors();

    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(buildPalette(c));
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    app.setStyleSheet(buildStyleSheet(c));
}

Theme::Mode Theme::loadMode()
{
    const QString saved = QSettings().value(QStringLiteral("ui/theme")).toString();
    return saved == QLatin1String("light") ? Mode::Light : Mode::Dark;
}

void Theme::saveMode(Mode mode)
{
    QSettings().setValue(QStringLiteral("ui/theme"),
                         mode == Mode::Light ? QStringLiteral("light")
                                             : QStringLiteral("dark"));
}

QColor Theme::channelColor(int index)
{
    if (index < 0)
        index = 0;
    return g_mode == Mode::Light ? kLightChannelColors[index % kChannelColorCount]
                                 : kDarkChannelColors[index % kChannelColorCount];
}