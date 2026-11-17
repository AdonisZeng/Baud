// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "DataExporter.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <cmath>

namespace {
// 数据列：10 位有效数字，足够无损还原 float32 采样值。
// 非有限值按原样写 nan / inf / -inf：此前 +Inf 被写成 "nan"，
// 会把“溢出”误读成“无效值”（CSV 导入端仍按既定策略拒绝非有限值）。
QString formatCell(double value)
{
    if (std::isnan(value))
        return QStringLiteral("nan");
    if (std::isinf(value))
        return value > 0.0 ? QStringLiteral("inf") : QStringLiteral("-inf");
    return QString::number(value, 'g', 10);
}

// NaN != NaN，但“连续相同值”里两个 NaN 应视为同一段
bool sameValue(double a, double b)
{
    return a == b || (std::isnan(a) && std::isnan(b));
}

QString columnHeader(int channelCount, bool compact)
{
    QStringList columns;
    if (compact)
        columns << QStringLiteral("t_start_s") << QStringLiteral("t_end_s");
    else
        columns << QStringLiteral("time_s");
    for (int channel = 0; channel < channelCount; ++channel)
        columns << QStringLiteral("ch%1").arg(channel);
    return columns.join(QStringLiteral(" | "));
}
} // namespace

DataExporter::DataExporter(QObject *parent)
    : QObject(parent)
{
}

DataExporter::~DataExporter() = default;

bool DataExporter::open(const QString &path, const QString &metaHeader, int channelCount,
                        int format)
{
    m_format = static_cast<Format>(format);
    if (m_format != Format::Markdown && m_format != Format::CompactMarkdown)
        m_format = Format::Csv;

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
    m_sampleCount = 0;
    m_runOpen = false;
    m_runValues.clear();

    // 元数据头：CSV 用 # 注释行（Excel / pandas 都能正常打开），
    // Markdown 由 GUI 侧生成普通标题与列表。
    if (!metaHeader.isEmpty())
        *m_stream << metaHeader;

    writeTableHeader();

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

void DataExporter::writeTableHeader()
{
    const bool compact = m_format == Format::CompactMarkdown;
    if (m_format == Format::Csv) {
        QStringList header;
        header << QStringLiteral("time_s");
        for (int channel = 0; channel < m_channelCount; ++channel)
            header << QStringLiteral("ch%1").arg(channel);
        *m_stream << header.join(QLatin1Char(',')) << Qt::endl;
        return;
    }

    *m_stream << QStringLiteral("| ") << columnHeader(m_channelCount, compact)
              << QStringLiteral(" |") << Qt::endl;

    QStringList separators;
    const int columns = m_channelCount + (compact ? 2 : 1);
    for (int column = 0; column < columns; ++column)
        separators << QStringLiteral("---:");
    *m_stream << QStringLiteral("| ") << separators.join(QStringLiteral(" | "))
              << QStringLiteral(" |") << Qt::endl;
}

void DataExporter::appendChunk(const QVector<double> &times, const QVector<double> &values)
{
    if (!m_stream || m_failed)
        return;

    if (m_format == Format::CompactMarkdown)
        appendCompactChunk(times, values);
    else
        writeSampleRows(times, values);

    // 写盘失败（磁盘满、权限不足、设备移除）必须立即收尾，
    // 否则 GUI 侧会一直等不到 finished，界面停在“正在导出”状态。
    if (m_stream->status() != QTextStream::Ok || m_file->error() != QFileDevice::NoError) {
        failNow();
        return;
    }

    emit needMore();
}

void DataExporter::writeSampleRows(const QVector<double> &times, const QVector<double> &values)
{
    const int rows = times.size();
    for (int row = 0; row < rows; ++row) {
        const int base = row * m_channelCount;
        if (m_format == Format::Csv) {
            *m_stream << QString::number(times.at(row), 'f', 6);
            for (int channel = 0; channel < m_channelCount; ++channel) {
                *m_stream << QLatin1Char(',');
                const int index = base + channel;
                *m_stream << formatCell(index < values.size() ? values.at(index) : 0.0);
            }
        } else {
            *m_stream << QStringLiteral("| ") << QString::number(times.at(row), 'f', 6);
            for (int channel = 0; channel < m_channelCount; ++channel) {
                const int index = base + channel;
                *m_stream << QStringLiteral(" | ")
                          << formatCell(index < values.size() ? values.at(index) : 0.0);
            }
            *m_stream << QStringLiteral(" |");
        }
        *m_stream << Qt::endl;
        ++m_rowCount;
    }

    if (rows > 0)
        emit rowsWritten(m_rowCount);
}

void DataExporter::appendCompactChunk(const QVector<double> &times, const QVector<double> &values)
{
    const int rows = times.size();
    for (int row = 0; row < rows; ++row) {
        const int base = row * m_channelCount;

        bool same = m_runOpen && m_runValues.size() == m_channelCount;
        for (int channel = 0; channel < m_channelCount && same; ++channel) {
            const int index = base + channel;
            same = sameValue(index < values.size() ? values.at(index) : 0.0,
                             m_runValues.at(channel));
        }

        ++m_sampleCount;
        if (same) {
            // 整行与当前区间相同：只延长时间段的结束时刻
            m_runEnd = times.at(row);
            continue;
        }

        if (m_runOpen)
            flushRun();

        m_runValues.resize(m_channelCount);
        for (int channel = 0; channel < m_channelCount; ++channel) {
            const int index = base + channel;
            m_runValues[channel] = index < values.size() ? values.at(index) : 0.0;
        }
        m_runStart = times.at(row);
        m_runEnd = m_runStart;
        m_runOpen = true;
    }
}

void DataExporter::flushRun()
{
    if (!m_runOpen || !m_stream)
        return;

    *m_stream << QStringLiteral("| ") << QString::number(m_runStart, 'f', 6)
              << QStringLiteral(" | ") << QString::number(m_runEnd, 'f', 6);
    for (int channel = 0; channel < m_runValues.size(); ++channel)
        *m_stream << QStringLiteral(" | ") << formatCell(m_runValues.at(channel));
    *m_stream << QStringLiteral(" |") << Qt::endl;

    m_runOpen = false;
    ++m_rowCount;
    emit rowsWritten(m_rowCount);
}

void DataExporter::failNow()
{
    m_failed = true;
    const QString message =
        QStringLiteral("写入文件失败：%1")
            .arg(m_file && m_file->error() != QFileDevice::NoError
                     ? m_file->errorString()
                     : QStringLiteral("磁盘已满或文件不可写"));
    m_stream.reset();
    if (m_file) {
        m_file->close();
        m_file.reset();
    }
    emit finished(false, message);
}

void DataExporter::close()
{
    if (m_failed)
        return;   // 错误已经在 open/appendChunk 里报过，不能重复发 finished

    if (m_format == Format::CompactMarkdown && m_runOpen)
        flushRun();

    // Markdown 收尾备注：把行数/段数写进文件，方便 AI 或人快速核对内容规模
    if (m_stream && m_format != Format::Csv) {
        *m_stream << Qt::endl;
        if (m_format == Format::CompactMarkdown) {
            *m_stream << QStringLiteral("<!-- 精简导出：%1 个时间段，覆盖 %2 个采样点 -->")
                             .arg(m_rowCount)
                             .arg(m_sampleCount)
                      << Qt::endl;
        } else {
            *m_stream << QStringLiteral("<!-- 共 %1 个采样点 -->").arg(m_rowCount) << Qt::endl;
        }
    }

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

    if (writeOk) {
        const QString message =
            m_format == Format::CompactMarkdown
                ? QStringLiteral("已导出 %1 段（覆盖 %2 个采样点）")
                      .arg(m_rowCount)
                      .arg(m_sampleCount)
                : QStringLiteral("已导出 %1 行").arg(m_rowCount);
        emit finished(true, message);
    } else {
        emit finished(false, errorText.isEmpty() ? QStringLiteral("写入文件失败") : errorText);
    }
}
