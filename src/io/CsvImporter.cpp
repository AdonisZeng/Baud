// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "CsvImporter.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace {
// 每批送回的帧数，兼顾内存占用与信号频率
constexpr int kBatchFrames = 4096;
} // namespace

CsvImporter::CsvImporter(QObject *parent)
    : QObject(parent)
{
}

void CsvImporter::importFile(const QString &path)
{
    m_path = path;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit imported(false, QStringLiteral("无法打开文件：%1").arg(path), 0, 0);
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    // 文件总字节数，用于估算进度
    const qint64 totalBytes = file.size();
    int lastPercent = -1;
    const auto reportProgress = [&]() {
        if (totalBytes <= 0)
            return;
        // QTextStream::pos() 是流内的字符位置，与字节数不完全一致（中文注释会偏小），
        // 只用于展示进度，不影响解析结果。
        const int percent = qBound(0, static_cast<int>(stream.pos() * 100 / totalBytes), 99);
        if (percent != lastPercent) {
            lastPercent = percent;
            emit progressChanged(percent);
        }
    };

    // 跳过元数据注释行，找到表头
    QStringList header;
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        // 去掉 UTF-8 BOM：Excel 另存为“UTF-8 CSV”会带 BOM，
        // 不剥离的话首行不会被识别为注释行，会被误当成表头。
        if (line.startsWith(QChar(0xFEFF)))
            line.remove(0, 1);
        if (line.startsWith(QLatin1Char('#')))
            continue;
        if (line.trimmed().isEmpty())
            continue;
        header = line.split(QLatin1Char(','));
        break;
    }

    if (header.size() < 2) {
        emit imported(false, QStringLiteral("文件格式不正确：缺少数据表头"), 0, 0);
        return;
    }

    // 表头第一列固定为 time_s，其余为通道
    const int channelCount = header.size() - 1;

    FrameBatch batch;
    batch.reserve(kBatchFrames);
    qint64 rowCount = 0;
    double lastTime = 0.0;
    bool hasLastTime = false;

    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.trimmed().isEmpty())
            continue;

        const QStringList parts = line.split(QLatin1Char(','));
        if (parts.size() < header.size())
            continue;

        bool ok = false;
        const double time = parts.at(0).trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(time))
            continue;

        Frame frame;
        frame.t = time;
        frame.values.reserve(channelCount);
        bool valid = true;
        for (int channel = 0; channel < channelCount; ++channel) {
            bool valueOk = false;
            const double value = parts.at(channel + 1).trimmed().toDouble(&valueOk);
            if (!valueOk || !std::isfinite(value)) {
                valid = false;
                break;
            }
            frame.values.append(value);
        }
        if (!valid)
            continue;

        // 时间戳必须严格递增，否则绘图时 X 坐标会重复
        if (hasLastTime && frame.t <= lastTime)
            frame.t = lastTime + 1e-9;
        lastTime = frame.t;
        hasLastTime = true;

        batch.append(std::move(frame));
        ++rowCount;

        if (batch.size() >= kBatchFrames) {
            emit framesParsed(batch);
            batch.clear();
            batch.reserve(kBatchFrames);
            reportProgress();
        }
    }

    if (!batch.isEmpty())
        emit framesParsed(batch);

    emit progressChanged(100);
    emit imported(true, QStringLiteral("已导入 %1 行").arg(rowCount), rowCount, channelCount);
}
