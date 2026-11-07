// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace {
const QColor kChannelColors[] = {
    QColor("#4C9AFF"),
    QColor("#FF8A3D"),
    QColor("#4CD964"),
    QColor("#B07CFF"),
    QColor("#2DD4BF"),
    QColor("#FFD166"),
    QColor("#FF5D8F"),
    QColor("#A0AEC0"),
};
} // namespace

void Theme::apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#1B1F24"));
    palette.setColor(QPalette::WindowText, QColor("#E6EAF0"));
    palette.setColor(QPalette::Base, QColor("#12161A"));
    palette.setColor(QPalette::AlternateBase, QColor("#171C22"));
    palette.setColor(QPalette::ToolTipBase, QColor("#242A31"));
    palette.setColor(QPalette::ToolTipText, QColor("#E6EAF0"));
    palette.setColor(QPalette::Text, QColor("#E6EAF0"));
    palette.setColor(QPalette::Button, QColor("#242A31"));
    palette.setColor(QPalette::ButtonText, QColor("#E6EAF0"));
    palette.setColor(QPalette::BrightText, QColor("#FF6B6B"));
    palette.setColor(QPalette::Link, QColor("#4C9AFF"));
    palette.setColor(QPalette::Highlight, QColor("#2F6FED"));
    palette.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#7A8591"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#7A8591"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#7A8591"));
    app.setPalette(palette);

    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));

    app.setStyleSheet(QStringLiteral(R"(
QWidget {
    font-family: "Microsoft YaHei UI";
    font-size: 9pt;
}
QMainWindow {
    background: #1B1F24;
}
QGroupBox {
    background: #20262D;
    border: 1px solid #2A313A;
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
    color: #C8D2DC;
}
QLabel {
    color: #C8D2DC;
}
QPushButton {
    min-height: 28px;
    padding: 0 12px;
    border: 1px solid #343C47;
    border-radius: 6px;
    background: #2A313A;
    color: #E6EAF0;
}
QPushButton:hover {
    background: #333B46;
    border-color: #46515E;
}
QPushButton:pressed {
    background: #232A32;
}
QPushButton:disabled {
    background: #242A31;
    color: #6F7B87;
    border-color: #2A313A;
}
QPushButton#primaryButton {
    background: #2F6FED;
    border-color: #2F6FED;
    color: #FFFFFF;
    font-weight: 600;
}
QPushButton#primaryButton:hover {
    background: #4C84F5;
    border-color: #4C84F5;
}
QPushButton#primaryButton:pressed {
    background: #2457C7;
    border-color: #2457C7;
}
QPushButton#primaryButton:disabled {
    background: #2A3448;
    border-color: #2A3448;
    color: #7E8DA0;
}
QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
    min-height: 26px;
    padding: 0 8px;
    border: 1px solid #343C47;
    border-radius: 6px;
    background: #171C22;
    color: #E6EAF0;
    selection-background-color: #2F6FED;
}
QComboBox:hover, QLineEdit:hover {
    border-color: #46515E;
}
QComboBox:disabled, QLineEdit:disabled {
    color: #6F7B87;
    background: #1B2027;
}
QComboBox::drop-down {
    width: 22px;
    border: none;
}
QComboBox QAbstractItemView {
    background: #20262D;
    border: 1px solid #343C47;
    selection-background-color: #2F6FED;
    outline: none;
}
QCheckBox {
    color: #C8D2DC;
    spacing: 6px;
}
QTableWidget {
    background: #171C22;
    alternate-background-color: #1B2128;
    border: 1px solid #2A313A;
    border-radius: 6px;
    gridline-color: #2A313A;
    color: #E6EAF0;
    selection-background-color: #2F6FED;
}
QHeaderView::section {
    background: #20262D;
    color: #A9B4C0;
    border: none;
    border-bottom: 1px solid #2A313A;
    padding: 4px 6px;
    font-weight: 600;
}
QPlainTextEdit {
    background: #10151A;
    color: #C8D2DC;
    border: 1px solid #2A313A;
    border-radius: 6px;
    selection-background-color: #2F6FED;
}
QScrollBar:vertical {
    background: #171C22;
    width: 10px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #3A434F;
    min-height: 24px;
    border-radius: 5px;
}
QScrollBar::handle:vertical:hover {
    background: #4B5664;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar:horizontal {
    background: #171C22;
    height: 10px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #3A434F;
    min-width: 24px;
    border-radius: 5px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}
QStatusBar {
    background: #171C22;
    color: #A9B4C0;
    border-top: 1px solid #2A313A;
}
QMenuBar {
    background: #1B1F24;
    color: #C8D2DC;
}
QMenuBar::item:selected {
    background: #2A313A;
}
QMenu {
    background: #20262D;
    color: #E6EAF0;
    border: 1px solid #343C47;
}
QMenu::item:selected {
    background: #2F6FED;
}
QToolTip {
    background: #242A31;
    color: #E6EAF0;
    border: 1px solid #343C47;
}
)"));
}

QColor Theme::channelColor(int index)
{
    const int count = static_cast<int>(sizeof(kChannelColors) / sizeof(kChannelColors[0]));
    if (index < 0)
        return kChannelColors[0];
    return kChannelColors[index % count];
}
