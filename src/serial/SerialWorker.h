// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "SerialSettings.h"
#include "protocol/IProtocolParser.h"
#include "protocol/ProtocolTypes.h"

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
    void errorOccurred(const QString &message);
    void rawBytesReceived(const QByteArray &bytes);
    void framesReceived(const FrameBatch &frames);

private slots:
    void onReadyRead();
    void flushPending();
    void onSerialError(QSerialPort::SerialPortError error);

private:
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
};
