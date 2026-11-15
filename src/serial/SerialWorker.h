// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "FrameTimestamper.h"
#include "SerialSettings.h"
#include "protocol/IProtocolParser.h"
#include "protocol/ProtocolTypes.h"

#include <QElapsedTimer>
#include <QObject>

#include <memory>

class QSerialPort;
class QTimer;

// 在独立线程中持有 QSerialPort 与解析器，按批次向 GUI 发送数据
class SerialWorker : public QObject {
    Q_OBJECT

public:
    explicit SerialWorker(QObject *parent = nullptr);
    ~SerialWorker() override;

public slots:
    void openPort(const SerialSettings &settings, ProtocolMode mode);
    void closePort();
    void changeProtocol(ProtocolMode mode);
    void writeBytes(const QByteArray &bytes);

signals:
    void opened();
    void closed();
    void protocolApplied();
    void errorOccurred(const QString &message);
    void rawBytesReceived(const QByteArray &bytes);
    void framesReceived(const FrameBatch &frames);
    // 实测采样周期（秒）与抖动（秒），由接收节奏自动推算
    void timingUpdated(double period, double jitter);

private slots:
    void onReadyRead();
    void flushPending();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    // 周期/抖动上报节流：串口批次可能非常密集，
    // 每批都发信号会把 GUI 线程淹没
    bool shouldReportTiming(double now);

    void closePortInternal(bool notify);

    static constexpr int kFlushIntervalMs = 16;
    static constexpr int kRawPendingLimit = 1024 * 1024;
    static constexpr int kPendingFrameLimit = 20000;

    QSerialPort *m_port = nullptr;
    std::unique_ptr<IProtocolParser> m_parser;
    ProtocolMode m_mode = ProtocolMode::RawData;
    QTimer *m_flushTimer = nullptr;
    QByteArray m_pendingRaw;
    FrameBatch m_pendingFrames;
    bool m_open = false;

    // 单调时钟，用于给每帧打时间戳
    QElapsedTimer m_clock;
    // 时间戳重建与采样周期估计
    FrameTimestamper m_timestamper;
    // 上报节流：记录上次上报时刻（秒，连接后）与是否已上报过
    double m_lastTimingReportTime = 0.0;
    bool m_timingReported = false;
};
