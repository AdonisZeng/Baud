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

    if (m_stream->status() != QTextStream::Ok) {
        m_failed = true;
        m_stream.reset();
        m_file->close();
        m_file.reset();
        emit finished(false, QStringLiteral("无法写入文件：%1").arg(path));
        return false;
    }

    // 导出是“导出线程按需拉取”的模型：needMore 触发 GUI 线程切片，
    // 再由 appendChunk 把数据推回来。这里必须主动发出第一次请求，
    // 否则一块数据都不会被请求，文件里只剩元数据和表头。
    emit needMore();
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

    // 写盘失败（磁盘满、权限不足、设备移除）必须立即收尾，
    // 否则 GUI 侧会一直等不到 finished，界面停在“正在导出”状态。
    if (m_stream->status() != QTextStream::Ok || m_file->error() != QFileDevice::NoError) {
        m_failed = true;
        const QString message =
            QStringLiteral("写入文件失败：%1")
                .arg(m_file->error() != QFileDevice::NoError
                         ? m_file->errorString()
                         : QStringLiteral("磁盘已满或文件不可写"));
        m_stream.reset();
        m_file->close();
        m_file.reset();
        emit finished(false, message);
        return;
    }

    emit needMore();
}

void CsvExporter::close()
{
    bool writeOk = true;
    QString errorText;

    if (m_stream) {
        m_stream->flush();
        if (m_stream->status() != QTextStream::Ok) {
            writeOk = false;
            errorText = QStringLiteral("写入文件失败：磁盘已满或文件不可写");
        }
        m_stream.reset();
    }
    if (m_file) {
        m_file->close();
        // 有些写盘错误（磁盘写满、设备移除）只在 flush/close 阶段才暴露，
        // 此时 QTextStream 的状态可能仍是 Ok，必须单独检查 QFile。
        if (m_file->error() != QFileDevice::NoError) {
            writeOk = false;
            if (errorText.isEmpty())
                errorText = QStringLiteral("写入文件失败：%1").arg(m_file->errorString());
        }
        m_file.reset();
    }

    if (m_failed)
        return;   // 错误已经在 open/appendChunk 里报过，不能重复发 finished

    if (writeOk)
        emit finished(true, QStringLiteral("已导出 %1 行").arg(m_rowCount));
    else
        emit finished(false, errorText.isEmpty() ? QStringLiteral("写入文件失败") : errorText);
}
