// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "model/ChannelConfig.h"

#include <QDialog>

class QLabel;
class QTableWidget;

// 通道配置对话框：
//   · 通道页：自定义通道名称、给通道指定类型（状态码 / 错误码）
//   · 类型页：新增 / 删除通道类型，编辑每个类型的码名与码值
// 编辑在副本上进行，确定后由调用方取走（config()）。
class ChannelConfigDialog : public QDialog {
    Q_OBJECT

public:
    ChannelConfigDialog(const ChannelConfig &config, int channelCount,
                        QWidget *parent = nullptr);

    const ChannelConfig &config() const { return m_config; }

private:
    void buildUi();
    void reloadChannelTable();
    void reloadTypeTable();
    void commitChannelTable();
    void commitTypeTable();
    void onAddType();
    void onRemoveType();
    void onEditCodes(int row);

    ChannelConfig m_config;
    int m_channelCount = 0;
    QLabel *m_channelHint = nullptr;
    QTableWidget *m_channelTable = nullptr;
    QTableWidget *m_typeTable = nullptr;
};
