// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "SerialWorker.h"

#include "protocol/ParserFactory.h"

#include <QSerialPort>
#include <QTimer>

SerialWorker::SerialWorker(QObject *parent)
    : QObject(parent)
{
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(kFlushIntervalMs);
    m_flushTimer->setSingleShot(false);
    connect(m_flushTimer, &QTimer::timeout, this, &SerialWorker::flushPending);
}

SerialWorker::~SerialWorker()
{
    closePortInternal(false);
}

void SerialWorker::openPort(const SerialSettings &settings, ProtocolMode mode)
{
    closePortInternal(true);

    m_mode = mode;
    m_parser = createParser(mode);

    // QSerialPort 必须在工作线程内创建
    m_port = new QSerialPort(this);
    m_port->setPortName(settings.portName);
    m_port->setBaudRate(settings.baudRate);
    m_port->setDataBits(settings.dataBits);
    m_port->setParity(settings.parity);
    m_port->setStopBits(settings.stopBits);
    m_port->setFlowControl(settings.flowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        const QString message = QStringLiteral("无法打开串口 %1：%2")
                                    .arg(settings.portName, m_port->errorString());
        delete m_port;
        m_port = nullptr;
        m_parser.reset();
        emit errorOccurred(message);
        return;
    }

    connect(m_port, &QSerialPort::readyRead, this, &SerialWorker::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &SerialWorker::onSerialError);

    m_open = true;
    m_flushTimer->start();
    emit opened();
}

void SerialWorker::closePort()
{
    closePortInternal(true);
}

void SerialWorker::closePortInternal(bool notify)
{
    if (!m_port)
        return;

    if (m_flushTimer)
        m_flushTimer->stop();

    flushPending();

    m_open = false;
    m_port->close();
    delete m_port;
    m_port = nullptr;
    m_parser.reset();
    m_pendingRaw.clear();
    m_pendingFrames.clear();

    if (notify)
        emit closed();
}

void SerialWorker::changeProtocol(ProtocolMode mode)
{
    if (m_mode == mode)
        return;

    m_mode = mode;
    m_parser = createParser(mode);
    m_pendingFrames.clear();
}

void SerialWorker::writeBytes(const QByteArray &bytes)
{
    if (!m_open || !m_port) {
        emit errorOccurred(QStringLiteral("串口未连接，无法发送数据"));
        return;
    }
    m_port->write(bytes);
}

void SerialWorker::onReadyRead()
{
    if (!m_port)
        return;

    const QByteArray chunk = m_port->readAll();
    if (chunk.isEmpty())
        return;

    m_pendingRaw.append(chunk);
    if (m_pendingRaw.size() > kRawPendingLimit)
        m_pendingRaw = m_pendingRaw.right(kRawPendingLimit);

    if (!m_parser)
        return;

    FrameBatch frames = m_parser->feed(chunk);
    if (!frames.isEmpty()) {
        m_pendingFrames += frames;
        if (m_pendingFrames.size() > kPendingFrameLimit)
            flushPending();
    }
}

void SerialWorker::flushPending()
{
    if (!m_pendingRaw.isEmpty()) {
        emit rawBytesReceived(m_pendingRaw);
        m_pendingRaw.clear();
    }
    if (!m_pendingFrames.isEmpty()) {
        emit framesReceived(m_pendingFrames);
        m_pendingFrames.clear();
    }
}

void SerialWorker::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_port)
        return;

    const QString message = QStringLiteral("串口错误：%1").arg(m_port->errorString());

    switch (error) {
    case QSerialPort::ResourceError:
    case QSerialPort::PermissionError:
    case QSerialPort::DeviceNotFoundError:
        emit errorOccurred(message);
        closePortInternal(true);
        break;
    default:
        emit errorOccurred(message);
        break;
    }
}
