// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// GitHub Release 中与自动更新相关的信息
struct UpdateRelease {
    QString version;   // 去掉前缀 v 的版本号，如 1.2.0
    QString tagName;   // 原始 tag，如 v1.2.0
    QString name;
    QString body;
    QString htmlUrl;
    QUrl setupAsset;    // Baud-Setup-*.exe
    QUrl checksumAsset; // SHA256SUMS.txt
    QString setupAssetName;    // 与 setupAsset 的 URL 文件名绑定
    QString checksumAssetName; // 与 checksumAsset 的 URL 文件名绑定
    bool isValid() const { return !version.isEmpty(); }
};

class UpdateChecker : public QObject {
    Q_OBJECT

public:
    // 安装状态：区分可静默更新的安装版与便携运行
    struct InstallState {
        bool installed = false;        // 存在卸载注册项
        bool runningInstalled = false; // 当前 exe 位于安装目录
        QString installPath;
        QString scope; // "user" / "machine"
    };

    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    static QString currentVersion();
    static bool shouldAutoCheck();
    static InstallState detectInstall();
    static bool launchInstaller(const QString &installerPath, const QString &scope,
                                QString *errorMessage = nullptr);

    bool isBusy() const;
    void checkForUpdates();
    void downloadInstaller(const UpdateRelease &release);
    void abort();

signals:
    void checkSucceeded(const UpdateRelease &release, bool updateAvailable);
    void checkFailed(const QString &message);
    void downloadProgress(qint64 received, qint64 total);
    void downloadSucceeded(const QString &installerPath);
    void downloadFailed(const QString &message);

private:
    void handleCheckFinished();
    void handleChecksumFinished();
    void handleDownloadFinished();
    void failCheck(const QString &message);
    void failDownload(const QString &message);
    void markChecked();
    void startChecksumDownload();
    void startInstallerDownload();
    void cleanupDownload();
    QString downloadErrorText(QNetworkReply *reply) const;

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_checkReply = nullptr;
    QNetworkReply *m_downloadReply = nullptr;
    UpdateRelease m_pendingRelease;
    QString m_targetPath;
    QString m_expectedHash;
    bool m_cancelled = false;
};
