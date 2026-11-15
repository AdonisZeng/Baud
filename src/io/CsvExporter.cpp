// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "CsvExporter.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

CsvExporter::CsvExporter(QObject *parent)
    : QObject(parent)
{
}

CsvExporter::~CsvExporter() = default;

bool CsvExporter::open(const QString &path, const QString &metaHeader, int channelCount)
{
    m_file = std::make_unique<QFile>(path);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        m_failed = true;
        emit finished(false, QStringLiteral("无法写入文件：%1").arg(path));
        return false;
    }

    m_stream = std::make_unique<QTextStream>(m_file.get());
    m_stream->setEncoding(QStringConverter::Utf8);
    m_channelCount = channelCount;
    m_rowCount = 0;

    // 元数据头：以 # 开头的注释行，Excel / pandas 都能正常打开
    if (!metaHeader.isEmpty())
        *m_stream << metaHeader;

    QStringList header;
    header << QStringLiteral("time_s");
    for (int channel = 0; channel < channelCount; ++channel)
        header << QStringLiteral("ch%1").arg(channel);
    *m_stream << header.join(QLatin1Char(',')) << Qt::endl;

    return true;
}

void CsvExporter::appendChunk(const QVector<double> &times, const QVector<double> &values)
{
    if (!m_stream || m_failed)
        return;

    const int rows = times.size();
    for (int row = 0; row < rows; ++row) {
        *m_stream << QString::number(times.at(row), 'f', 6);
        const int base = row * m_channelCount;
        for (int channel = 0; channel < m_channelCount; ++channel) {
            *m_stream << QLatin1Char(',');
            const int index = base + channel;
            if (index < values.size())
                *m_stream << QString::number(values.at(index), 'g', 10);
            else
                *m_stream << QStringLiteral("0");
        }
        *m_stream << Qt::endl;
    }

    m_rowCount += rows;
    emit rowsWritten(m_rowCount);
    emit needMore();
}

void CsvExporter::close()
{
    if (m_stream) {
        m_stream->flush();
        m_stream.reset();
    }
    if (m_file) {
        m_file->close();
        m_file.reset();
    }
    if (!m_failed)
        emit finished(true, QStringLiteral("已导出 %1 行").arg(m_rowCount));
}
