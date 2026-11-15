// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "protocol/ProtocolTypes.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

// CSV 导入器：在独立线程读取 Baud 导出的 CSV，
// 解析出带时间戳的帧批次，回送到 GUI 线程灌入 ChannelStore。
class CsvImporter : public QObject {
    Q_OBJECT

public:
    explicit CsvImporter(QObject *parent = nullptr);

public slots:
    // 开始导入；解析完成后发出 imported()
    void importFile(const QString &path);

signals:
    // 分批送回解析出的帧
    void framesParsed(const FrameBatch &frames);
    void progressChanged(int percent);
    void imported(bool ok, const QString &message, qint64 rowCount, int channelCount);

private:
    QString m_path;
};
