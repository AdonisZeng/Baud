// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

class QFile;
class QTextStream;

// 数据导出器：CSV / Markdown / 精简 Markdown 三种格式，运行在独立线程，
// 逐块接收数据并写盘。数据由 GUI 线程从 ChannelStore 切片后推送过来，
// 避免一次拷贝全部缓冲（20 万点 x 64 通道可达 100 MB），
// 也避免在 GUI 线程做大量字符串格式化导致界面卡顿。
//
// 精简 Markdown 把“所有通道值都不变”的连续采样合并成一行时间段
// （t_start_s | t_end_s | ch0 | ch1 | …），文件体积可小几个数量级，
// 也便于直接粘给 AI 阅读。
class DataExporter : public QObject {
    Q_OBJECT

public:
    enum class Format {
        Csv = 0,             // 宽表 time_s,ch0,ch1,…
        Markdown = 1,        // Markdown 表格，每行一个采样点
        CompactMarkdown = 2  // Markdown 表格，每行一段连续不变的时间区间
    };

    explicit DataExporter(QObject *parent = nullptr);
    ~DataExporter() override;

public slots:
    // 打开文件并写入元数据头；format 取 Format 的整数值（跨线程调用用 int）
    bool open(const QString &path, const QString &metaHeader, int channelCount,
              int format = static_cast<int>(Format::Csv));
    // 写入一块数据：times 为每行时间（秒），values 按行主序展开
    void appendChunk(const QVector<double> &times, const QVector<double> &values);
    // 结束写入并关闭文件
    void close();

signals:
    // 需要下一块数据
    void needMore();
    // 已写入的行数（精简格式下为时间段数）
    void rowsWritten(qint64 rows);
    void finished(bool ok, const QString &message);

private:
    void writeTableHeader();
    void writeSampleRows(const QVector<double> &times, const QVector<double> &values);
    void appendCompactChunk(const QVector<double> &times, const QVector<double> &values);
    // 输出当前累计的“连续相同值”区间（精简 Markdown 用）
    void flushRun();
    void failNow();

    std::unique_ptr<QFile> m_file;
    std::unique_ptr<QTextStream> m_stream;
    Format m_format = Format::Csv;
    int m_channelCount = 0;
    qint64 m_rowCount = 0;     // CSV/Markdown：采样行数；精简：时间段数
    qint64 m_sampleCount = 0;  // 精简格式下累计覆盖的采样点
    bool m_failed = false;

    // 精简 Markdown 中尚未收尾的区间
    bool m_runOpen = false;
    double m_runStart = 0.0;
    double m_runEnd = 0.0;
    QVector<double> m_runValues;
};
