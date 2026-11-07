// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include <QMetaType>
#include <QSerialPort>
#include <QString>

// 串口打开参数
struct SerialSettings {
    QString portName;
    qint32 baudRate = 2000000;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
};

Q_DECLARE_METATYPE(SerialSettings)
