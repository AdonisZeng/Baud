// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng
//
// 帧时间戳、周期估计与数据导出/导入的独立自测（不依赖串口硬件）。
// 用法：scripts\run-selftest.ps1

#include "serial/FrameTimestamper.h"
#include "model/ChannelStore.h"
#include "io/DataExporter.h"
#include "io/CsvImporter.h"
#include "ui/ValueFormat.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <cmath>
#include <limits>

static int g_failures = 0;
static QFile g_log;

static void check(bool ok, const QString &what)
{
    if (!ok) {
        ++g_failures;
        g_log.write(QStringLiteral("FAIL: %1\n").arg(what).toUtf8());
    } else {
        g_log.write(QStringLiteral("ok  : %1\n").arg(what).toUtf8());
    }
    g_log.flush();
}

static FrameBatch makeFrames(int count)
{
    FrameBatch frames;
    for (int i = 0; i < count; ++i) {
        Frame frame;
        frame.values << i;
        frames.append(frame);
    }
    return frames;
}

// 推进一批：更新时间戳并检查单调性
static void feed(FrameTimestamper &stamper, double now, int count,
                 double &prevLast, bool &monotonic)
{
    FrameBatch frames = makeFrames(count);
    stamper.onChunk(now, count);
    stamper.stamp(frames, now);
    for (const Frame &frame : frames) {
        if (prevLast >= 0.0 && frame.t <= prevLast)
            monotonic = false;
        prevLast = frame.t;
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    g_log.setFileName(QDir::tempPath() + "/baud_timestamper_test.txt");
    if (!g_log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return 1;
    }

    // ---- 0. 单调时钟纳秒 -> 秒换算（曾把毫秒当秒，时间轴放大 1000 倍）----
    {
        check(std::fabs(FrameTimestamper::secondsFromNanoseconds(1'000'000'000) - 1.0) < 1e-12,
              "纳秒换算：1e9 ns == 1 s");
        check(std::fabs(FrameTimestamper::secondsFromNanoseconds(16'000'000) - 0.016) < 1e-12,
              "纳秒换算：16e6 ns == 16 ms");
        check(std::fabs(FrameTimestamper::secondsFromNanoseconds(1'000'000) - 0.001) < 1e-12,
              "纳秒换算：1e6 ns == 1 ms");
    }

    // ---- 0b. 数值显示：定点区间内不要过早切科学计数 ----
    {
        check(ValueFormat::format(1234.5) == QStringLiteral("1234.5"),
              QStringLiteral("数值显示：1234.5 不丢有效位（%1）")
                  .arg(ValueFormat::format(1234.5)));
        check(ValueFormat::format(12345.0) == QStringLiteral("12345"),
              QStringLiteral("数值显示：12345 仍是定点（%1）")
                  .arg(ValueFormat::format(12345.0)));
        check(ValueFormat::format(9999999.0) == QStringLiteral("9999999"),
              QStringLiteral("数值显示：1e7 以下保持定点（%1）")
                  .arg(ValueFormat::format(9999999.0)));
        check(ValueFormat::format(0.000123456) == QStringLiteral("0.000123456"),
              QStringLiteral("数值显示：小数值保留有效位（%1）")
                  .arg(ValueFormat::format(0.000123456)));
        check(ValueFormat::format(1.5e8).contains(QStringLiteral("e+")),
              QStringLiteral("数值显示：超大值才用科学计数（%1）")
                  .arg(ValueFormat::format(1.5e8)));
        check(ValueFormat::format(std::nan("")) == QStringLiteral("--"),
              "数值显示：NaN 显示为 --");
    }

    // ---- 1. 稳态 1 kHz，批大小固定 ----
    {
        FrameTimestamper stamper;
        double now = 0.0;
        double prevLast = -1.0;
        bool monotonic = true;
        for (int batch = 0; batch < 300; ++batch) {
            now += 0.016;
            feed(stamper, now, 16, prevLast, monotonic);
        }
        check(monotonic, "稳态 1 kHz：时间戳严格递增");
        check(std::fabs(stamper.period() - 0.001) < 2e-5,
              QStringLiteral("稳态 1 kHz：周期估计 ~1 ms（实测 %1 ms）")
                  .arg(stamper.period() * 1000.0, 0, 'f', 4));
        check(stamper.jitter() < 5e-5,
              QStringLiteral("稳态 1 kHz：抖动 ~0（实测 %1 us）")
                  .arg(stamper.jitter() * 1e6, 0, 'f', 2));
    }

    // ---- 2. 设备空闲 2 秒后突然来一批（周期不应被高估）----
    {
        FrameTimestamper stamper;
        double now = 0.0;
        double prevLast = -1.0;
        bool monotonic = true;
        for (int batch = 0; batch < 300; ++batch) {
            now += 0.016;
            feed(stamper, now, 16, prevLast, monotonic);
        }
        const double before = stamper.period();
        now += 2.0;                       // 空闲 2 秒
        feed(stamper, now, 16, prevLast, monotonic);
        check(std::fabs(stamper.period() - before) < 2e-5,
              QStringLiteral("空闲 2 秒后突发：周期未被高估（%1 ms）")
                  .arg(stamper.period() * 1000.0, 0, 'f', 4));
        check(monotonic, "空闲 2 秒后突发：时间戳仍严格递增");
    }

    // ---- 3. 变速：1 kHz -> 100 Hz ----
    {
        FrameTimestamper stamper;
        double now = 0.0;
        double prevLast = -1.0;
        bool monotonic = true;
        for (int batch = 0; batch < 300; ++batch) {
            now += 0.016;
            feed(stamper, now, 16, prevLast, monotonic);
        }
        // 改成 100 Hz：每批 2 帧，每 20 ms 一批
        for (int batch = 0; batch < 400; ++batch) {
            now += 0.02;
            feed(stamper, now, 2, prevLast, monotonic);
        }
        check(stamper.period() > 0.007 && stamper.period() < 0.013,
              QStringLiteral("变速到 100 Hz：周期收敛到 ~10 ms（实测 %1 ms）")
                  .arg(stamper.period() * 1000.0, 0, 'f', 3));
        check(monotonic, "变速过程：时间戳始终严格递增");
    }

    // ---- 4. 极端批大小：一次交付 500 帧，周期 0.1 ms ----
    {
        FrameTimestamper stamper;
        double now = 0.0;
        double prevLast = -1.0;
        bool monotonic = true;
        for (int batch = 0; batch < 300; ++batch) {
            now += 0.05;
            feed(stamper, now, 500, prevLast, monotonic);
        }
        check(monotonic, "单批 500 帧：时间戳仍严格递增");
        check(std::fabs(stamper.period() - 0.0001) < 2e-5,
              QStringLiteral("单批 500 帧/50 ms：周期 ~0.1 ms（实测 %1 us）")
                  .arg(stamper.period() * 1e6, 0, 'f', 2));
    }

    // ---- 5. 批大小波动：帧数时多时少，但速率恒定 ----
    {
        FrameTimestamper stamper;
        double now = 0.0;
        double prevLast = -1.0;
        bool monotonic = true;
        const int sizes[] = {1, 7, 32, 3, 16, 5, 28, 9};
        int index = 0;
        for (int batch = 0; batch < 400; ++batch) {
            const int count = sizes[index++ % 8];
            now += count * 0.001;         // 速率恒为 1 kHz
            feed(stamper, now, count, prevLast, monotonic);
        }
        check(monotonic, "批大小波动：时间戳严格递增");
        check(std::fabs(stamper.period() - 0.001) < 2e-5,
              QStringLiteral("批大小波动：周期仍为 ~1 ms（实测 %1 ms）")
                  .arg(stamper.period() * 1000.0, 0, 'f', 4));
    }

    // ---- 6. 时间戳灌入 ChannelStore 后可按时间反查 ----
    {
        FrameTimestamper stamper;
        double now = 0.0;
        double prevLast = -1.0;
        bool monotonic = true;
        ChannelStore store;
        for (int batch = 0; batch < 100; ++batch) {
            now += 0.016;
            FrameBatch frames = makeFrames(16);
            stamper.onChunk(now, frames.size());
            stamper.stamp(frames, now);
            store.appendFrames(frames);
            for (const Frame &frame : frames) {
                if (prevLast >= 0.0 && frame.t <= prevLast)
                    monotonic = false;
                prevLast = frame.t;
            }
        }
        check(store.sampleCount() == 1600, "1600 帧全部入库");
        check(monotonic, "入库过程时间戳严格递增");
        check(store.indexAtTime(0.800) >= 790 && store.indexAtTime(0.800) <= 810,
              "按时间反查索引正确");
        check(store.nearestIndexAtTime(0.8005) >= 799, "最近邻索引查找正确");
        check(std::fabs(store.estimatedPeriod() - stamper.period()) < 1e-4,
              "Store 估计周期与时间戳器一致");
    }

    // ---- 7. CSV 导出 -> 导入 往返 ----
    {
        const QString path = QDir::tempPath() + "/baud_csv_roundtrip.csv";
        QFile::remove(path);

        const int channelCount = 3;
        const int rowCount = 5;
        QVector<double> times;
        QVector<double> values;
        for (int row = 0; row < rowCount; ++row) {
            times.append(0.001 * (row + 1));
            for (int channel = 0; channel < channelCount; ++channel)
                values.append(row * 10.0 + channel);
        }

        DataExporter exporter;
        bool exportOk = false;
        QObject::connect(&exporter, &DataExporter::finished,
                         [&exportOk](bool ok, const QString &) { exportOk = ok; });
        exporter.open(path, QStringLiteral("# Baud Export v1\n"), channelCount);
        exporter.appendChunk(times, values);
        exporter.close();

        check(exportOk, "CSV 导出成功");
        check(QFile::exists(path), "CSV 文件已生成");

        // 文件内容：注释头 + 列名 + 数据行
        QFile file(path);
        QStringList lines;
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
            file.close();
        }
        check(!lines.isEmpty() && lines.at(0).startsWith(QLatin1Char('#')),
              "CSV 首行是元数据注释");
        check(lines.size() > 1 && lines.at(1).startsWith(QStringLiteral("time_s")),
              "CSV 第二行是列名（time_s 开头）");

        // 导入回来比对
        CsvImporter importer;
        FrameBatch imported;
        bool importOk = false;
        qint64 importedRows = 0;
        int importedChannels = 0;
        QObject::connect(&importer, &CsvImporter::framesParsed,
                         [&imported](const FrameBatch &batch) { imported += batch; });
        QObject::connect(&importer, &CsvImporter::imported,
                         [&](bool ok, const QString &, qint64 rows, int channels) {
                             importOk = ok;
                             importedRows = rows;
                             importedChannels = channels;
                         });
        importer.importFile(path);

        check(importOk, "CSV 导入成功");
        check(importedRows == rowCount,
              QStringLiteral("往返行数一致（导出 %1 / 导入 %2）").arg(rowCount).arg(importedRows));
        check(importedChannels == channelCount,
              QStringLiteral("往返通道数一致（导出 %1 / 导入 %2）")
                  .arg(channelCount).arg(importedChannels));
        check(imported.size() == rowCount, "往返帧数一致");

        bool valuesMatch = imported.size() == rowCount;
        for (int row = 0; row < imported.size() && valuesMatch; ++row) {
            if (std::fabs(imported.at(row).t - times.at(row)) > 1e-6)
                valuesMatch = false;
            for (int channel = 0; channel < channelCount && valuesMatch; ++channel) {
                const double expected = values.at(row * channelCount + channel);
                if (std::fabs(imported.at(row).values.at(channel) - expected) > 1e-6)
                    valuesMatch = false;
            }
        }
        check(valuesMatch, "往返后时间戳与通道值一致");

        QFile::remove(path);
    }

    // ---- 7b. 导出握手：open() 之后必须主动请求第一块数据 ----
    // 真实链路是 needMore -> GUI 线程切片 -> appendChunk；像第 7 节那样直接
    // 调 appendChunk 会掩盖“open 后一块数据都没被请求”的回归（文件只剩表头）。
    {
        const QString path = QDir::tempPath() + "/baud_csv_handshake.csv";
        QFile::remove(path);

        const int channelCount = 2;
        const int rowCount = 3;
        QVector<double> times;
        QVector<double> values;
        for (int row = 0; row < rowCount; ++row) {
            times.append(0.001 * (row + 1));
            for (int channel = 0; channel < channelCount; ++channel)
                values.append(row * 10.0 + channel);
        }

        DataExporter exporter;
        bool exportOk = false;
        bool chunkSent = false;
        int needMoreCount = 0;
        QObject::connect(&exporter, &DataExporter::needMore, [&]() {
            ++needMoreCount;
            if (!chunkSent) {
                chunkSent = true;
                exporter.appendChunk(times, values);
            }
        });
        QObject::connect(&exporter, &DataExporter::finished,
                         [&exportOk](bool ok, const QString &) { exportOk = ok; });

        check(exporter.open(path, QStringLiteral("# Baud Export v1\n"), channelCount),
              "握手用例：open 成功");
        check(needMoreCount >= 1 && chunkSent, "open 后立即请求第一块数据");
        exporter.close();
        check(exportOk, "握手用例：导出完成");

        int dataRows = 0;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QStringList lines =
                QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                if (!line.isEmpty() && !line.startsWith(QLatin1Char('#'))
                    && !line.startsWith(QStringLiteral("time_s")))
                    ++dataRows;
            }
            file.close();
        }
        check(dataRows == rowCount,
              QStringLiteral("握手用例：文件数据行数正确（%1）").arg(dataRows));

        QFile::remove(path);
    }

    // ---- 8. 带 UTF-8 BOM 的 CSV 也能正确导入 ----
    {
        const QString path = QDir::tempPath() + "/baud_csv_bom.csv";
        QFile::remove(path);

        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            const QByteArray content = QByteArray("\xEF\xBB\xBF")   // UTF-8 BOM
                + "# Baud Export v1\n"
                + "time_s,ch0,ch1\n"
                + "0.001,1.5,2.5\n";
            file.write(content);
            file.close();
        }

        CsvImporter importer;
        FrameBatch imported;
        bool importOk = false;
        int importedChannels = 0;
        QObject::connect(&importer, &CsvImporter::framesParsed,
                         [&imported](const FrameBatch &batch) { imported += batch; });
        QObject::connect(&importer, &CsvImporter::imported,
                         [&](bool ok, const QString &, qint64, int channels) {
                             importOk = ok;
                             importedChannels = channels;
                         });
        importer.importFile(path);

        check(importOk, "带 BOM 的 CSV 导入成功");
        check(importedChannels == 2, "带 BOM 的 CSV 通道数正确");
        check(imported.size() == 1 && std::fabs(imported.first().values.at(0) - 1.5) < 1e-9,
              "带 BOM 的 CSV 数据解析正确");

        QFile::remove(path);
    }

    // ---- 9. Markdown 导出：逐点表格 + ch0 起始列名 ----
    {
        const QString path = QDir::tempPath() + "/baud_export.md";
        QFile::remove(path);

        const int channelCount = 2;
        const int rowCount = 3;
        QVector<double> times;
        QVector<double> values;
        for (int row = 0; row < rowCount; ++row) {
            times.append(0.001 * (row + 1));
            for (int channel = 0; channel < channelCount; ++channel)
                values.append(row + channel * 0.5);
        }

        DataExporter exporter;
        bool exportOk = false;
        QObject::connect(&exporter, &DataExporter::finished,
                         [&exportOk](bool ok, const QString &) { exportOk = ok; });
        check(exporter.open(path, QStringLiteral("# Baud 数据导出\n\n"), channelCount,
                            static_cast<int>(DataExporter::Format::Markdown)),
              "Markdown 导出：open 成功");
        exporter.appendChunk(times, values);
        exporter.close();
        check(exportOk, "Markdown 导出成功");

        QString text;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            text = QString::fromUtf8(file.readAll());
            file.close();
        }
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        const QStringList lines = text.split(QLatin1Char('\n'));

        int dataRows = 0;
        bool hasHeader = false;
        bool hasSeparator = false;
        for (const QString &line : lines) {
            if (line.startsWith(QStringLiteral("| 0.")))
                ++dataRows;
            if (line == QStringLiteral("| time_s | ch0 | ch1 |"))
                hasHeader = true;
            if (line.startsWith(QStringLiteral("| ---:")))
                hasSeparator = true;
        }
        check(dataRows == rowCount,
              QStringLiteral("Markdown 数据行数正确（%1）").arg(dataRows));
        check(hasHeader, "Markdown 表头列名从 ch0 开始");
        check(hasSeparator, "Markdown 表头带对齐分隔行");

        QFile::remove(path);
    }

    // ---- 10. 精简 Markdown：连续相同值合并成时间段（跨块也要合并）----
    {
        const QString path = QDir::tempPath() + "/baud_export_compact.md";
        QFile::remove(path);

        const int channelCount = 2;
        // 三组区间：0.001~0.002 → (1,2)；0.003~0.004 → (1,3)；0.005 → (2,3)
        QVector<double> times = {0.001, 0.002, 0.003, 0.004, 0.005};
        QVector<double> values = {1, 2, 1, 2, 1, 3, 1, 3, 2, 3};

        DataExporter exporter;
        bool exportOk = false;
        QString message;
        QObject::connect(&exporter, &DataExporter::finished,
                         [&](bool ok, const QString &text) { exportOk = ok; message = text; });
        check(exporter.open(path, QStringLiteral("# Baud 数据导出（精简）\n\n"), channelCount,
                            static_cast<int>(DataExporter::Format::CompactMarkdown)),
              "精简 Markdown：open 成功");

        // 故意按两块推送：第 2、3 行跨块且取值相同，必须合并成同一段
        exporter.appendChunk(times.mid(0, 2), values.mid(0, 4));
        exporter.appendChunk(times.mid(2, 3), values.mid(4, 6));
        exporter.close();

        check(exportOk, "精简 Markdown 导出成功");
        check(message.contains(QStringLiteral("3 段")),
              QStringLiteral("精简 Markdown 段数正确（%1）").arg(message));

        QString text;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            text = QString::fromUtf8(file.readAll());
            file.close();
        }
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        const QStringList lines = text.split(QLatin1Char('\n'));

        check(lines.contains(QStringLiteral("| t_start_s | t_end_s | ch0 | ch1 |")),
              "精简 Markdown 表头包含起止时间列");
        check(lines.contains(QStringLiteral("| 0.001000 | 0.002000 | 1 | 2 |")),
              "精简 Markdown：首段区间合并正确");
        check(lines.contains(QStringLiteral("| 0.003000 | 0.004000 | 1 | 3 |")),
              "精简 Markdown：跨块相同值合并正确");
        check(lines.contains(QStringLiteral("| 0.005000 | 0.005000 | 2 | 3 |")),
              "精简 Markdown：单点区间的起止时间相同");

        int dataRows = 0;
        for (const QString &line : lines) {
            if (line.startsWith(QStringLiteral("| 0.")))
                ++dataRows;
        }
        check(dataRows == 3, QStringLiteral("精简 Markdown 数据行数正确（%1）").arg(dataRows));

        QFile::remove(path);
    }

    // ---- 11. 非有限值导出：nan / inf / -inf 必须能区分 ----
    {
        const QString path = QDir::tempPath() + "/baud_export_nonfinite.csv";
        QFile::remove(path);

        QVector<double> times = {0.001, 0.002, 0.003};
        QVector<double> values = {std::numeric_limits<double>::quiet_NaN(),
                                  std::numeric_limits<double>::infinity(),
                                  -std::numeric_limits<double>::infinity()};

        DataExporter exporter;
        check(exporter.open(path, QString(), 1,
                            static_cast<int>(DataExporter::Format::Csv)),
              "非有限值：open 成功");
        exporter.appendChunk(times, values);
        exporter.close();

        QString text;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            text = QString::fromUtf8(file.readAll());
            file.close();
        }
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        const QStringList lines = text.split(QLatin1Char('\n'));

        check(lines.contains(QStringLiteral("0.001000,nan")), "CSV 导出：NaN 保持为 nan");
        check(lines.contains(QStringLiteral("0.002000,inf")), "CSV 导出：+Inf 写成 inf");
        check(lines.contains(QStringLiteral("0.003000,-inf")), "CSV 导出：-Inf 写成 -inf");

        QFile::remove(path);
    }

    g_log.write(QStringLiteral("%1\n")
                    .arg(g_failures == 0 ? QStringLiteral("ALL PASS")
                                        : QStringLiteral("FAILURES"))
                    .toUtf8());
    g_log.flush();
    g_log.close();
    return g_failures == 0 ? 0 : 1;
}
