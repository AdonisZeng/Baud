// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "Baud.h"
#include "Version.h"
#include "protocol/ProtocolTypes.h"
#include "serial/SerialSettings.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QIcon>
#include <QMetaType>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Baud"));
    QApplication::setOrganizationName(QStringLiteral("Baud"));
    QApplication::setApplicationVersion(QStringLiteral(BAUD_VERSION_STRING));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/Baud/Baud.ico")));

    qRegisterMetaType<Frame>("Frame");
    qRegisterMetaType<FrameBatch>("FrameBatch");
    qRegisterMetaType<ProtocolMode>("ProtocolMode");
    qRegisterMetaType<SerialSettings>("SerialSettings");

    Theme::apply(app);

    Baud window;
    window.show();
    return app.exec();
}
