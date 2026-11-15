// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

class QFile;
class QTextStream;

// CSV 导出器：运行在独立线程，逐块接收数据并写盘。
// 数据由 GUI 线程从 ChannelStore 切片后推送过来，
// 避免一次拷贝全部缓冲（20 万点 x 64 通道可达 100 MB），
// 也避免在 GUI 线程做大量字符串格式化导致界面卡顿。
class CsvExporter : public QObject {
    Q_OBJECT

public:
    explicit CsvExporter(QObject *parent = nullptr);
    ~CsvExporter() override;

public slots:
    // 打开文件并写入元数据头
    bool open(const QString &path, const QString &metaHeader, int channelCount);
    // 写入一块数据：times 为每行时间（秒），values 按行主序展开
    void appendChunk(const QVector<double> &times, const QVector<double> &values);
    // 结束写入并关闭文件
    void close();

signals:
    // 需要下一块数据
    void needMore();
    // 已写入的行数
    void rowsWritten(qint64 rows);
    void finished(bool ok, const QString &message);

private:
    std::unique_ptr<QFile> m_file;
    std::unique_ptr<QTextStream> m_stream;
    int m_channelCount = 0;
    qint64 m_rowCount = 0;
    bool m_failed = false;
};
