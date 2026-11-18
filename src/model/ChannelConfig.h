// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include <QString>
#include <QVector>

// 单个码值：状态码 / 错误码等
struct ChannelCode {
    QString name;       // 码名，如 RUN
    double value = 0.0; // 码值，如 6
};

// 通道类型：不同工程、不同工程师的状态码/错误码定义都不一样，
// 因此类型与码表都允许用户自行新增、修改。
struct ChannelType {
    QString id;   // 唯一标识，如 foc.state
    QString name; // 显示名，如 "运行状态"
    QVector<ChannelCode> codes;
};

// 单个通道的显示配置
struct ChannelSetting {
    QString displayName; // 自定义名称（空 → CH0）
    QString typeId;      // 通道类型 ID（空 → 纯数值，不做码值解释）
};

// 通道配置：通道名称、通道类型与码表。
// 持久化在 QSettings 的 channels/config（JSON），下次启动自动恢复。
class ChannelConfig {
public:
    static constexpr int kMaxChannels = 64;

    ChannelConfig();

    void load();
    void save() const;

    // ---- 通道类型 ----
    int typeCount() const;
    const ChannelType *typeAt(int index) const;
    int indexOfType(const QString &id) const;
    int addType(const QString &id, const QString &name);
    bool removeType(int index);
    void setTypeInfo(int index, const QString &id, const QString &name);
    void setCodes(int index, const QVector<ChannelCode> &codes);

    // ---- 通道 ----
    QString channelName(int channel) const; // 自定义名，未设置时返回 CH{n}
    // 原始自定义名：未设置时为空（配置对话框用它显示“留空即默认名”）
    QString channelDisplayName(int channel) const;
    void setChannelName(int channel, const QString &name);
    QString channelTypeId(int channel) const;
    void setChannelType(int channel, const QString &typeId);

    // 数值 → 码名；未命中码表时返回空字符串
    QString codeName(int channel, double value) const;

private:
    void applyDefaults();
    const ChannelSetting *settingAt(int channel) const;
    ChannelSetting &settingFor(int channel);

    QVector<ChannelType> m_types;
    QVector<ChannelSetting> m_channels;
};
