// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng
//
// 帧时间戳、周期估计与数据导出/导入的独立自测（不依赖串口硬件）。
// 用法：scripts\run-selftest.ps1

#include "serial/FrameTimestamper.h"
#include "model/ChannelStore.h"
#include "model/ChannelConfig.h"
#include "io/DataExporter.h"
#include "io/CsvImporter.h"
#include "ui/ValueFormat.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>

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
    // 通道配置用 QSettings 持久化：自测改用临时目录里的 INI 文件，
    // 避免写注册表（没有组织名时 QSettings 根本没有可用路径）。
    QCoreApplication::setOrganizationName(QStringLiteral("BaudSelfTest"));
    QCoreApplication::setApplicationName(QStringLiteral("BaudSelfTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());

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

    // ---- 6b. 缓冲窗口：按时间裁剪数据（硬上限，超出即丢）----
    {
        ChannelStore store;
        FrameBatch frames;
        for (int i = 0; i < 1000; ++i) {
            Frame frame;
            frame.t = 0.001 * (i + 1);   // 1 kHz，共 1 s
            frame.values = {static_cast<double>(i)};
            frames.append(frame);
        }
        store.appendFrames(frames);
        check(store.sampleCount() == 1000, "缓冲窗口：未设置时全部保留");

        store.setRetentionSeconds(0.1);   // 只留最近 100 ms
        const qint64 first = store.firstStoredIndex();
        const qint64 last = store.sampleCount() - 1;
        check(store.sampleCount() == 1000, "缓冲窗口：总样本计数不回退");
        // 时间戳由浮点累加而来，边界样本可能落在 cutoff 两侧，允许 ±1
        check(first >= 899 && first <= 901,
              QStringLiteral("缓冲窗口：只保留最近 100 个样本（实测 %1）").arg(first));
        check(store.timeAt(last) - store.timeAt(first) <= 0.1 + 1e-6,
              "缓冲窗口：窗口内时间跨度不超过缓冲时长");
        check(std::isnan(store.valueAt(0, 0)), "缓冲窗口：窗口外的旧数据不可再访问");
        check(store.valueAt(0, last) == 999.0, "缓冲窗口：最新数据仍在");

        store.setRetentionSeconds(0.05);
        const qint64 trimmedFirst = store.firstStoredIndex();
        check(trimmedFirst >= 949 && trimmedFirst <= 951,
              QStringLiteral("缓冲窗口：缩短窗口后立即再裁剪（实测 %1）").arg(trimmedFirst));

        store.setRetentionSeconds(0.0);
        check(store.firstStoredIndex() == trimmedFirst,
              "缓冲窗口：取消限制不会恢复已丢弃的数据");

        // 边接收边裁剪：追加新批次时也要把窗口外的旧样本丢掉
        ChannelStore live;
        live.setRetentionSeconds(0.05);
        live.appendFrames(frames.mid(0, 500));
        live.appendFrames(frames.mid(500));
        check(live.firstStoredIndex() >= 949 && live.firstStoredIndex() <= 951,
              QStringLiteral("缓冲窗口：边接收边裁剪（实测 %1）")
                  .arg(live.firstStoredIndex()));
        check(live.valueAt(0, live.sampleCount() - 1) == 999.0,
              "缓冲窗口：边接收边裁剪时最新数据仍在");
    }

    // ---- 6c. 从无限制切回有限制：m_firstIndex 落后时也必须按时间裁剪 ----
    // 典型路径：CSV 导入把 retention 置 0 灌满环形缓冲，用户随后改一次缓冲窗口。
    // 此时 m_firstIndex 仍停在 0，槽位已被新样本覆盖；若直接按时间扫描，
    // 会读到偏新的时间而提前 break，时间裁剪失效、只剩容量上限生效。
    {
        ChannelStore store;
        const int total = static_cast<int>(ChannelStore::kCapacity) + 1000;
        FrameBatch frames;
        frames.reserve(total);
        for (int i = 0; i < total; ++i) {
            Frame frame;
            frame.t = 0.001 * (i + 1);
            frame.values = {static_cast<double>(i)};
            frames.append(frame);
        }

        store.setRetentionSeconds(0.0);   // 模拟 CSV 导入：无时间裁剪
        store.appendFrames(frames);
        check(store.firstStoredIndex() == total - static_cast<int>(ChannelStore::kCapacity),
              "缓冲窗口：无限制时由容量上限决定最早样本");

        store.setRetentionSeconds(0.1);   // 用户改回 100 ms
        const qint64 kept = store.sampleCount() - store.firstStoredIndex();
        check(kept >= 99 && kept <= 101,
              QStringLiteral("缓冲窗口：从无限制切回限制后按时间裁剪（实测保留 %1 个）")
                  .arg(kept));
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
        check(lines.contains(QStringLiteral("| 0.001 | 0.002 | 1 | 2 |")),
              "精简 Markdown：首段区间合并正确");
        check(lines.contains(QStringLiteral("| 0.003 | 0.004 | 1 | 3 |")),
              "精简 Markdown：跨块相同值合并正确");
        check(lines.contains(QStringLiteral("| 0.005 | 0.005 | 2 | 3 |")),
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

        check(lines.contains(QStringLiteral("0.001,nan")), "CSV 导出：NaN 保持为 nan");
        check(lines.contains(QStringLiteral("0.002,inf")), "CSV 导出：+Inf 写成 inf");
        check(lines.contains(QStringLiteral("0.003,-inf")), "CSV 导出：-Inf 写成 -inf");

        QFile::remove(path);
    }

    // ---- 11b. 导入边界：非有限值 / 超通道数 / 空文件 / 大时间戳 ----
    {
        // 11b-1. 非有限值往返：自家导出的 nan / inf 必须能原样导回来
        const QString path = QDir::tempPath() + "/baud_csv_nonfinite_roundtrip.csv";
        QFile::remove(path);

        const QVector<double> times = {0.001, 0.002, 0.003};
        const QVector<double> values = {std::numeric_limits<double>::quiet_NaN(),
                                        std::numeric_limits<double>::infinity(),
                                        -std::numeric_limits<double>::infinity()};
        DataExporter exporter;
        exporter.open(path, QString(), 1, static_cast<int>(DataExporter::Format::Csv));
        exporter.appendChunk(times, values);
        exporter.close();

        CsvImporter importer;
        FrameBatch imported;
        bool importOk = false;
        qint64 importedRows = 0;
        QObject::connect(&importer, &CsvImporter::framesParsed,
                         [&imported](const FrameBatch &batch) { imported += batch; });
        QObject::connect(&importer, &CsvImporter::imported,
                         [&](bool ok, const QString &, qint64 rows, int) {
                             importOk = ok;
                             importedRows = rows;
                         });
        importer.importFile(path);

        check(importOk, "非有限值往返：导入成功");
        check(importedRows == 3,
              QStringLiteral("非有限值往返：行数不丢（%1）").arg(importedRows));
        check(imported.size() == 3, "非有限值往返：帧数不丢");
        bool nonFiniteKept = imported.size() == 3;
        if (nonFiniteKept) {
            nonFiniteKept = std::isnan(imported.at(0).values.at(0))
                && std::isinf(imported.at(1).values.at(0)) && imported.at(1).values.at(0) > 0.0
                && std::isinf(imported.at(2).values.at(0)) && imported.at(2).values.at(0) < 0.0;
        }
        check(nonFiniteKept, "非有限值往返：nan / inf / -inf 原样保留");
        QFile::remove(path);
    }

    // 11b-2. 超过 64 通道必须直接失败，不能“导入成功但缓冲区是空的”
    {
        const QString path = QDir::tempPath() + "/baud_csv_too_many_channels.csv";
        QFile::remove(path);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            QStringList header;
            header << QStringLiteral("time_s");
            for (int channel = 0; channel < 65; ++channel)
                header << QStringLiteral("ch%1").arg(channel);
            QByteArray content = header.join(QLatin1Char(',')).toUtf8() + "\n0.001";
            for (int channel = 0; channel < 65; ++channel)
                content += ",1";
            content += "\n";
            file.write(content);
            file.close();
        }

        CsvImporter importer;
        bool importOk = true;
        QString message;
        QObject::connect(&importer, &CsvImporter::imported,
                         [&](bool ok, const QString &text, qint64, int) {
                             importOk = ok;
                             message = text;
                         });
        importer.importFile(path);

        check(!importOk, "超通道数：导入被拒绝");
        check(message.contains(QStringLiteral("64")),
              QStringLiteral("超通道数：提示里写明上限（%1）").arg(message));
        QFile::remove(path);
    }

    // 11b-3. 只有表头没有数据行：必须报错，不能报“已导入 0 行”
    {
        const QString path = QDir::tempPath() + "/baud_csv_no_rows.csv";
        QFile::remove(path);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write("time_s,ch0\n");
            file.close();
        }

        CsvImporter importer;
        bool importOk = true;
        QObject::connect(&importer, &CsvImporter::imported,
                         [&](bool ok, const QString &, qint64, int) { importOk = ok; });
        importer.importFile(path);

        check(!importOk, "无数据行：导入报失败");
        QFile::remove(path);
    }

    // 11b-4. 大时间戳（Unix 秒）下的重复时间戳必须被修正成严格递增
    {
        const QString path = QDir::tempPath() + "/baud_csv_epoch.csv";
        QFile::remove(path);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            const double base = 1700000000.0;
            QByteArray content = "time_s,ch0\n";
            content += QString::number(base, 'f', 6).toUtf8() + ",1\n";
            content += QString::number(base, 'f', 6).toUtf8() + ",2\n";   // 与上一行重复
            content += QString::number(base + 0.001, 'f', 6).toUtf8() + ",3\n";
            file.write(content);
            file.close();
        }

        CsvImporter importer;
        FrameBatch imported;
        bool importOk = false;
        QObject::connect(&importer, &CsvImporter::framesParsed,
                         [&imported](const FrameBatch &batch) { imported += batch; });
        QObject::connect(&importer, &CsvImporter::imported,
                         [&](bool ok, const QString &, qint64, int) { importOk = ok; });
        importer.importFile(path);

        check(importOk, "大时间戳：导入成功");
        check(imported.size() == 3, "大时间戳：行数不丢");
        bool strictlyIncreasing = imported.size() == 3;
        for (int i = 1; i < imported.size() && strictlyIncreasing; ++i)
            strictlyIncreasing = imported.at(i).t > imported.at(i - 1).t;
        check(strictlyIncreasing, "大时间戳：重复时间戳被修正成严格递增");
        QFile::remove(path);
    }

    // 11b-5. double 无损往返：FireWater 的 double 不能只保留 10 位有效数字
    {
        const QString path = QDir::tempPath() + "/baud_csv_double_precision.csv";
        QFile::remove(path);

        const QVector<double> times = {0.001, 0.002};
        const QVector<double> values = {3.3, 1.0 / 3.0};
        DataExporter exporter;
        exporter.open(path, QString(), 1, static_cast<int>(DataExporter::Format::Csv));
        exporter.appendChunk(times, values);
        exporter.close();

        CsvImporter importer;
        FrameBatch imported;
        QObject::connect(&importer, &CsvImporter::framesParsed,
                         [&imported](const FrameBatch &batch) { imported += batch; });
        importer.importFile(path);

        bool exact = imported.size() == 2;
        if (exact) {
            exact = imported.at(0).values.at(0) == 3.3
                && imported.at(1).values.at(0) == 1.0 / 3.0
                && imported.at(0).t == 0.001
                && imported.at(1).t == 0.002;
        }
        check(exact, "double 无损往返：3.3 与 1/3 精确还原");
        QFile::remove(path);
    }

    // ---- 11c. 无损格式化的基本性质 ----
    {
        check(ValueFormat::roundTrip(3.3) == QStringLiteral("3.3"),
              QStringLiteral("无损格式：3.3 不被改写（%1）").arg(ValueFormat::roundTrip(3.3)));
        check(ValueFormat::roundTrip(0.001) == QStringLiteral("0.001"),
              QStringLiteral("无损格式：0.001 不被改写（%1）")
                  .arg(ValueFormat::roundTrip(0.001)));
        check(ValueFormat::roundTrip(1234.5678) == QStringLiteral("1234.5678"),
              QStringLiteral("无损格式：1234.5678 不被截断（%1）")
                  .arg(ValueFormat::roundTrip(1234.5678)));
        bool roundTrips = true;
        const double samples[] = {3.3, 1.0 / 3.0, 1.0 / 7.0, 1234.5678, 1e-9, 1e18, -0.125, 0.0};
        for (double sample : samples) {
            bool ok = false;
            const double parsed = ValueFormat::roundTrip(sample).toDouble(&ok);
            if (!ok || parsed != sample)
                roundTrips = false;
        }
        check(roundTrips, "无损格式：往返后数值完全一致");
    }

    // ---- 12. 通道配置：名称 / 类型 / 码值解码 / 持久化往返 ----
    {
        ChannelConfig config;
        check(config.indexOfType(QStringLiteral("foc.state")) >= 0,
              "通道配置：默认类型 foc.state 存在");
        check(config.indexOfType(QStringLiteral("prot_val.fault")) >= 0,
              "通道配置：默认类型 prot_val.fault 存在");

        config.setChannelName(0, QStringLiteral("速度"));
        check(config.channelName(0) == QStringLiteral("速度"),
              "通道配置：自定义名称生效");
        check(config.channelName(1) == QStringLiteral("CH1"),
              "通道配置：未改名的通道仍是 CH1");
        check(config.channelDisplayName(0) == QStringLiteral("速度")
                  && config.channelDisplayName(1).isEmpty(),
              "通道配置：原始名与回退名区分正确");

        config.setChannelType(0, QStringLiteral("foc.state"));
        check(config.codeName(0, 6.0) == QStringLiteral("RUN"),
              "通道配置：命中码表返回码名 RUN");
        check(config.codeName(0, 7.0).isEmpty(),
              "通道配置：未命中码表返回空");
        check(config.codeName(1, 6.0).isEmpty(),
              "通道配置：未指定类型的通道不做码值解释");

        // 新增自定义类型与码表
        const int typeIndex = config.addType(QStringLiteral("my.state"),
                                              QStringLiteral("自定义状态"));
        check(typeIndex >= 0, "通道配置：新增通道类型成功");
        QVector<ChannelCode> codes;
        codes.append({QStringLiteral("OK"), 0.0});
        codes.append({QStringLiteral("BUSY"), 3.0});
        config.setCodes(typeIndex, codes);
        config.setChannelType(1, QStringLiteral("my.state"));
        check(config.codeName(1, 3.0) == QStringLiteral("BUSY"),
              "通道配置：自定义类型码值解码正确");

        // 删除类型后引用它的通道要回到纯数值
        config.removeType(typeIndex);
        check(config.channelTypeId(1).isEmpty(),
              "通道配置：删除类型后通道引用被清空");

        // 持久化往返
        config.save();
        ChannelConfig loaded;
        loaded.load();
        check(loaded.channelName(0) == QStringLiteral("速度"),
              "通道配置：持久化后名称往返正确");
        check(loaded.codeName(0, 6.0) == QStringLiteral("RUN"),
              "通道配置：持久化后码表往返正确");

        // 改类型 ID：引用该类型的通道必须跟着改，否则会变成悬空引用
        config.setTypeInfo(config.indexOfType(QStringLiteral("foc.state")),
                           QStringLiteral("my.state"), QStringLiteral("我的状态"));
        check(config.channelTypeId(0) == QStringLiteral("my.state"),
              "通道配置：改类型 ID 后通道引用同步更新");
        check(config.codeName(0, 6.0) == QStringLiteral("RUN"),
              "通道配置：改类型 ID 后码值解释仍然有效");

        // 删光所有类型后保存，重新加载仍然是“没有类型”，不能冒出默认类型
        while (config.typeCount() > 0)
            config.removeType(0);
        config.save();
        ChannelConfig emptyLoaded;
        emptyLoaded.load();
        check(emptyLoaded.typeCount() == 0,
              QStringLiteral("通道配置：删光类型后持久化仍为空（实测 %1 个类型）")
                  .arg(emptyLoaded.typeCount()));
        check(emptyLoaded.codeName(0, 6.0).isEmpty(),
              "通道配置：没有类型时不做码值解释");

        // 清理测试写入的设置（自测用独立应用名与临时目录，不会影响正式程序）
        QSettings settings;
        settings.remove(QStringLiteral("channels/config"));
        settings.sync();
        QFile::remove(QDir::tempPath() + "/BaudSelfTest/BaudSelfTest.ini");
    }

    g_log.write(QStringLiteral("%1\n")
                    .arg(g_failures == 0 ? QStringLiteral("ALL PASS")
                                        : QStringLiteral("FAILURES"))
                    .toUtf8());
    g_log.flush();
    g_log.close();
    return g_failures == 0 ? 0 : 1;
}
