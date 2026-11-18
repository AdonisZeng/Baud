// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "CsvImporter.h"

#include "model/ChannelStore.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
// 每批送回的帧数，兼顾内存占用与信号频率
constexpr int kBatchFrames = 4096;

// 解析 CSV 单元格。QString::toDouble 对 "nan"/"inf" 的解析依赖 Qt 的区域设置实现，
// 这里显式兜底，保证自家导出的 nan / inf / -inf 能原样读回（否则往返会静默丢行）。
bool parseCell(const QString &text, double &value)
{
    const QString trimmed = text.trimmed();
    bool ok = false;
    value = trimmed.toDouble(&ok);
    if (ok)
        return true;

    const QString lower = trimmed.toLower();
    if (lower == QLatin1String("nan") || lower == QLatin1String("+nan")
        || lower == QLatin1String("-nan")) {
        value = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    if (lower == QLatin1String("inf") || lower == QLatin1String("+inf")
        || lower == QLatin1String("infinity") || lower == QLatin1String("+infinity")) {
        value = std::numeric_limits<double>::infinity();
        return true;
    }
    if (lower == QLatin1String("-inf") || lower == QLatin1String("-infinity")) {
        value = -std::numeric_limits<double>::infinity();
        return true;
    }
    return false;
}
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
    // 通道数上限与 ChannelStore 一致：超过上限的帧会被 ChannelStore 整帧丢弃，
    // 那样会出现“导入成功但缓冲区为空”的假象，必须在解析前就拒绝。
    if (channelCount > ChannelStore::kMaxChannels) {
        emit imported(false,
                      QStringLiteral("通道数 %1 超出上限，最多支持 %2 个通道。")
                          .arg(channelCount)
                          .arg(ChannelStore::kMaxChannels),
                      0, 0);
        return;
    }

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
            double value = 0.0;
            if (!parseCell(parts.at(channel + 1), value)) {
                valid = false;
                break;
            }
            // 非有限值（nan / inf）保留而不是丢整行：导出端本来就会写出这些值，
            // 整行跳过会让自家导出的文件往返后静默少掉采样点。
            // 绘图、悬停与极值统计本来就跳过非有限值，保留是安全的。
            frame.values.append(value);
        }
        if (!valid)
            continue;

        // 时间戳必须严格递增，否则绘图时 X 坐标会重复
        if (hasLastTime && frame.t <= lastTime) {
            double candidate = lastTime + 1e-9;
            // 大时间戳（如 Unix 秒）下 1e-9 加上去等于没加，
            // 必须退到 nextafter 才能拿到真正可表示的下一个值。
            if (candidate <= lastTime)
                candidate = std::nextafter(lastTime, std::numeric_limits<double>::infinity());
            if (!std::isfinite(candidate))
                continue;
            frame.t = candidate;
        }
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

    // 一行有效数据都没有：要么是空文件，要么整份文件都解析失败。
    // 报成功会让用户看到空白波形却以为导入完成，必须当成失败处理。
    if (rowCount == 0) {
        emit imported(false, QStringLiteral("文件中没有可导入的有效数据行。"), 0, channelCount);
        return;
    }

    emit progressChanged(100);
    emit imported(true, QStringLiteral("已导入 %1 行").arg(rowCount), rowCount, channelCount);
}
