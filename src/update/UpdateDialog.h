// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include "update/UpdateChecker.h"

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;

class UpdateDialog : public QDialog {
    Q_OBJECT

public:
    explicit UpdateDialog(UpdateChecker *checker, QWidget *parent = nullptr);

    // automatic 为 true 时，仅在发现新版本后显示
    void startCheck(bool automatic);

signals:
    void installLaunched();

protected:
    void reject() override;

private:
    enum class State { Checking, UpToDate, Available, Downloading, Failed };

    void buildUi();
    void setState(State state);
    void refreshUi();
    void showAndRaise();
    void startDownload();
    void cancelDownload();
    void openReleasePage();
    void setError(const QString &title, const QString &message);

    void onCheckSucceeded(const UpdateRelease &release, bool updateAvailable);
    void onCheckFailed(const QString &message);
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadSucceeded(const QString &installerPath);
    void onDownloadFailed(const QString &message);

    UpdateChecker *m_checker = nullptr;
    UpdateRelease m_release;
    State m_state = State::Checking;
    bool m_automatic = false;
    bool m_installStarted = false;

    QLabel *m_headline = nullptr;
    QLabel *m_detail = nullptr;
    QLabel *m_status = nullptr;
    QTextBrowser *m_notes = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_primaryButton = nullptr;
    QPushButton *m_secondaryButton = nullptr;
    QPushButton *m_pageButton = nullptr;
    QString m_errorTitle;
    QString m_errorMessage;
};
