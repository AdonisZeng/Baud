// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "model/ChannelConfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace {
// 配置在 QSettings 里的键
constexpr char kSettingsKey[] = "channels/config";

// 码值比较容差：状态码一般是整数，用很小的绝对容差兜住浮点误差。
// 不能用相对容差：码值到 1e9 量级时相对容差会放大到 1，可能误匹配相邻码值。
bool sameCodeValue(double a, double b)
{
    if (!std::isfinite(a) || !std::isfinite(b))
        return false;
    return std::fabs(a - b) <= 1e-9;
}
} // namespace

ChannelConfig::ChannelConfig()
{
    applyDefaults();
}

void ChannelConfig::applyDefaults()
{
    m_types.clear();
    m_channels.clear();

    // 两个示例类型：用户可以在“通道配置”里改名、改码表或删除。
    // 码表内容只是示例，实际含义由各工程自行定义。
    ChannelType state;
    state.id = QStringLiteral("foc.state");
    state.name = QStringLiteral("运行状态");
    state.codes.append({QStringLiteral("RUN"), 6.0});
    m_types.append(state);

    ChannelType fault;
    fault.id = QStringLiteral("prot_val.fault");
    fault.name = QStringLiteral("故障码");
    fault.codes.append({QStringLiteral("NONE"), 0.0});
    m_types.append(fault);

    m_channels.resize(kMaxChannels);
}

void ChannelConfig::load()
{
    QSettings settings;
    const QByteArray blob = settings.value(QLatin1String(kSettingsKey)).toByteArray();
    if (blob.isEmpty())
        return;

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(blob, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject root = doc.object();

    // 通道类型：必须区分“键不存在”（用默认类型）与“空列表”（用户删光了类型）。
    // 用 !types.isEmpty() 判断会让删光类型后重新冒出默认的 foc.state / prot_val.fault。
    if (root.contains(QStringLiteral("types"))) {
        const QJsonArray types = root.value(QStringLiteral("types")).toArray();
        QVector<ChannelType> loaded;
        loaded.reserve(types.size());
        for (const QJsonValue &value : types) {
            const QJsonObject object = value.toObject();
            ChannelType type;
            type.id = object.value(QStringLiteral("id")).toString();
            if (type.id.isEmpty())
                continue;
            type.name = object.value(QStringLiteral("name")).toString();
            const QJsonArray codes = object.value(QStringLiteral("codes")).toArray();
            for (const QJsonValue &codeValue : codes) {
                const QJsonObject codeObject = codeValue.toObject();
                const QString codeName = codeObject.value(QStringLiteral("name")).toString().trimmed();
                if (codeName.isEmpty())
                    continue;
                ChannelCode code;
                code.name = codeName;
                code.value = codeObject.value(QStringLiteral("value")).toDouble();
                type.codes.append(code);
            }
            loaded.append(type);
        }
        // 空数组表示用户删光了类型，不要退回默认类型
        m_types = loaded;
    }

    // 通道设置
    if (root.contains(QStringLiteral("channels"))) {
        const QJsonArray channels = root.value(QStringLiteral("channels")).toArray();
        m_channels.clear();
        m_channels.resize(kMaxChannels);
        for (const QJsonValue &value : channels) {
            const QJsonObject object = value.toObject();
            const int index = object.value(QStringLiteral("index")).toInt(-1);
            if (index < 0 || index >= kMaxChannels)
                continue;
            m_channels[index].displayName =
                object.value(QStringLiteral("name")).toString().trimmed();
            m_channels[index].typeId = object.value(QStringLiteral("type")).toString();
        }
        // 清理悬空引用：类型已不存在时通道退回纯数值显示
        for (ChannelSetting &setting : m_channels) {
            if (!setting.typeId.isEmpty() && indexOfType(setting.typeId) < 0)
                setting.typeId.clear();
        }
    }
}

void ChannelConfig::save() const
{
    QJsonArray types;
    for (const ChannelType &type : m_types) {
        if (type.id.isEmpty())
            continue;
        QJsonArray codes;
        for (const ChannelCode &code : type.codes) {
            if (code.name.isEmpty())
                continue;
            QJsonObject codeObject;
            codeObject.insert(QStringLiteral("name"), code.name);
            codeObject.insert(QStringLiteral("value"), code.value);
            codes.append(codeObject);
        }
        QJsonObject typeObject;
        typeObject.insert(QStringLiteral("id"), type.id);
        typeObject.insert(QStringLiteral("name"), type.name);
        typeObject.insert(QStringLiteral("codes"), codes);
        types.append(typeObject);
    }

    QJsonArray channels;
    for (int channel = 0; channel < m_channels.size(); ++channel) {
        const ChannelSetting &setting = m_channels.at(channel);
        if (setting.displayName.isEmpty() && setting.typeId.isEmpty())
            continue;
        QJsonObject object;
        object.insert(QStringLiteral("index"), channel);
        object.insert(QStringLiteral("name"), setting.displayName);
        object.insert(QStringLiteral("type"), setting.typeId);
        channels.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("types"), types);
    root.insert(QStringLiteral("channels"), channels);

    QSettings settings;
    settings.setValue(QLatin1String(kSettingsKey),
                      QJsonDocument(root).toJson(QJsonDocument::Compact));
}

int ChannelConfig::typeCount() const
{
    return m_types.size();
}

const ChannelType *ChannelConfig::typeAt(int index) const
{
    if (index < 0 || index >= m_types.size())
        return nullptr;
    return &m_types.at(index);
}

int ChannelConfig::indexOfType(const QString &id) const
{
    for (int index = 0; index < m_types.size(); ++index) {
        if (m_types.at(index).id == id)
            return index;
    }
    return -1;
}

int ChannelConfig::addType(const QString &id, const QString &name)
{
    const QString trimmed = id.trimmed();
    if (trimmed.isEmpty() || indexOfType(trimmed) >= 0)
        return -1;

    ChannelType type;
    type.id = trimmed;
    type.name = name.trimmed().isEmpty() ? trimmed : name.trimmed();
    m_types.append(type);
    return m_types.size() - 1;
}

bool ChannelConfig::removeType(int index)
{
    if (index < 0 || index >= m_types.size())
        return false;

    const QString id = m_types.at(index).id;
    m_types.removeAt(index);

    // 引用该类型的通道改回纯数值，避免残留悬空引用
    for (ChannelSetting &setting : m_channels) {
        if (setting.typeId == id)
            setting.typeId.clear();
    }
    return true;
}

void ChannelConfig::setTypeInfo(int index, const QString &id, const QString &name)
{
    if (index < 0 || index >= m_types.size())
        return;

    const QString trimmed = id.trimmed();
    if (trimmed.isEmpty()) {
        // 类型 ID 不允许为空（对话框保存时会提示），这里保持原 ID，只改显示名
        m_types[index].name =
            name.trimmed().isEmpty() ? m_types.at(index).id : name.trimmed();
        return;
    }

    const QString oldId = m_types.at(index).id;
    m_types[index].id = trimmed;
    m_types[index].name = name.trimmed().isEmpty() ? trimmed : name.trimmed();

    if (oldId == trimmed)
        return;

    // 改了类型 ID：把引用旧 ID 的通道一起改到新 ID。
    // 不做重映射的话，这些通道会变成悬空引用，悄悄退回纯数值显示。
    for (ChannelSetting &setting : m_channels) {
        if (setting.typeId == oldId)
            setting.typeId = trimmed;
    }
}

void ChannelConfig::setCodes(int index, const QVector<ChannelCode> &codes)
{
    if (index < 0 || index >= m_types.size())
        return;
    m_types[index].codes = codes;
}

const ChannelSetting *ChannelConfig::settingAt(int channel) const
{
    if (channel < 0 || channel >= m_channels.size())
        return nullptr;
    return &m_channels.at(channel);
}

ChannelSetting &ChannelConfig::settingFor(int channel)
{
    if (m_channels.size() < kMaxChannels)
        m_channels.resize(kMaxChannels);
    const int index = std::clamp(channel, 0, static_cast<int>(m_channels.size()) - 1);
    return m_channels[index];
}

QString ChannelConfig::channelName(int channel) const
{
    const ChannelSetting *setting = settingAt(channel);
    if (setting && !setting->displayName.isEmpty())
        return setting->displayName;
    return QStringLiteral("CH%1").arg(channel);
}

QString ChannelConfig::channelDisplayName(int channel) const
{
    const ChannelSetting *setting = settingAt(channel);
    return setting ? setting->displayName : QString();
}

void ChannelConfig::setChannelName(int channel, const QString &name)
{
    if (channel < 0 || channel >= kMaxChannels)
        return;
    settingFor(channel).displayName = name.trimmed();
}

QString ChannelConfig::channelTypeId(int channel) const
{
    const ChannelSetting *setting = settingAt(channel);
    return setting ? setting->typeId : QString();
}

void ChannelConfig::setChannelType(int channel, const QString &typeId)
{
    if (channel < 0 || channel >= kMaxChannels)
        return;
    settingFor(channel).typeId = typeId;
}

QString ChannelConfig::codeName(int channel, double value) const
{
    const ChannelSetting *setting = settingAt(channel);
    if (!setting || setting->typeId.isEmpty())
        return QString();

    const int typeIndex = indexOfType(setting->typeId);
    if (typeIndex < 0)
        return QString();

    const ChannelType &type = m_types.at(typeIndex);
    for (const ChannelCode &code : type.codes) {
        if (sameCodeValue(code.value, value))
            return code.name;
    }
    return QString();
}
