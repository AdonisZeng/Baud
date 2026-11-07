// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "UpdateDialog.h"

#include <QDesktopServices>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QString formatBytes(qint64 bytes)
{
    if (bytes >= 1024LL * 1024)
        return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    return QStringLiteral("%1 B").arg(bytes);
}
} // namespace

UpdateDialog::UpdateDialog(UpdateChecker *checker, QWidget *parent)
    : QDialog(parent)
    , m_checker(checker)
{
    setWindowTitle(QStringLiteral("检查更新"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowModality(Qt::NonModal);
    setMinimumWidth(560);
    buildUi();

    connect(m_checker, &UpdateChecker::checkSucceeded,
            this, &UpdateDialog::onCheckSucceeded);
    connect(m_checker, &UpdateChecker::checkFailed,
            this, &UpdateDialog::onCheckFailed);
    connect(m_checker, &UpdateChecker::downloadProgress,
            this, &UpdateDialog::onDownloadProgress);
    connect(m_checker, &UpdateChecker::downloadSucceeded,
            this, &UpdateDialog::onDownloadSucceeded);
    connect(m_checker, &UpdateChecker::downloadFailed,
            this, &UpdateDialog::onDownloadFailed);
}

void UpdateDialog::buildUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);

    m_headline = new QLabel(this);
    QFont headlineFont = m_headline->font();
    headlineFont.setPointSizeF(headlineFont.pointSizeF() + 2.0);
    headlineFont.setBold(true);
    m_headline->setFont(headlineFont);

    m_detail = new QLabel(this);
    m_detail->setWordWrap(true);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_notes = new QTextBrowser(this);
    m_notes->setMinimumHeight(180);
    m_notes->setMaximumHeight(260);
    m_notes->setOpenExternalLinks(true);
    m_notes->setVisible(false);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(true);
    m_progress->setVisible(false);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color: #A9B4C0;"));
    m_status->setVisible(false);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);

    m_pageButton = new QPushButton(QStringLiteral("打开发布页面"), this);
    m_secondaryButton = new QPushButton(QStringLiteral("关闭"), this);
    m_primaryButton = new QPushButton(QStringLiteral("下载并安装"), this);
    m_primaryButton->setObjectName(QStringLiteral("primaryButton"));

    buttons->addWidget(m_pageButton);
    buttons->addWidget(m_secondaryButton);
    buttons->addWidget(m_primaryButton);

    layout->addWidget(m_headline);
    layout->addWidget(m_detail);
    layout->addWidget(m_notes);
    layout->addStretch(1);
    layout->addWidget(m_progress);
    layout->addWidget(m_status);
    layout->addLayout(buttons);

    connect(m_primaryButton, &QPushButton::clicked, this, [this]() {
        if (m_state != State::Available)
            return;
        const UpdateChecker::InstallState install = UpdateChecker::detectInstall();
        if (install.runningInstalled && m_release.setupAsset.isValid())
            startDownload();
        else
            openReleasePage();
    });
    connect(m_pageButton, &QPushButton::clicked, this, &UpdateDialog::openReleasePage);
    connect(m_secondaryButton, &QPushButton::clicked, this, [this]() {
        if (m_state == State::Downloading) {
            cancelDownload();
            return;
        }
        if (m_state == State::Checking)
            m_checker->abort();
        reject();
    });

    setState(State::Checking);
}

void UpdateDialog::startCheck(bool automatic)
{
    if (m_checker->isBusy())
        return;

    m_automatic = automatic;
    m_release = UpdateRelease();
    m_errorTitle.clear();
    m_errorMessage.clear();
    setState(State::Checking);

    if (!automatic)
        showAndRaise();

    m_checker->checkForUpdates();
}

void UpdateDialog::setState(State state)
{
    m_state = state;
    refreshUi();
}

void UpdateDialog::refreshUi()
{
    const UpdateChecker::InstallState install = UpdateChecker::detectInstall();
    const bool hasNotes = m_state == State::Available && !m_release.body.trimmed().isEmpty();

    m_notes->setVisible(hasNotes);
    m_progress->setVisible(m_state == State::Checking || m_state == State::Downloading);
    m_status->setVisible(m_state == State::Downloading && !m_status->text().isEmpty());
    m_primaryButton->setVisible(false);
    m_primaryButton->setEnabled(true);
    m_pageButton->setVisible(false);
    m_secondaryButton->setVisible(true);
    m_secondaryButton->setEnabled(true);
    m_detail->setStyleSheet(QStringLiteral("color: #C8D2DC;"));

    switch (m_state) {
    case State::Checking:
        m_headline->setText(QStringLiteral("正在检查更新"));
        m_detail->setText(QStringLiteral("当前版本 %1，正在查询 GitHub Releases。")
                              .arg(UpdateChecker::currentVersion()));
        m_progress->setRange(0, 0);
        m_secondaryButton->setText(QStringLiteral("取消"));
        break;

    case State::UpToDate:
        m_headline->setText(QStringLiteral("已是最新版本"));
        m_detail->setText(QStringLiteral("当前版本 %1，没有可用的更新。")
                              .arg(UpdateChecker::currentVersion()));
        m_secondaryButton->setText(QStringLiteral("关闭"));
        break;

    case State::Available:
        m_headline->setText(QStringLiteral("发现新版本 %1").arg(m_release.version));
        if (install.runningInstalled) {
            m_detail->setText(
                QStringLiteral("当前版本 %1，最新版本 %2。下载完成后将自动校验 "
                               "SHA256 并启动安装程序。")
                    .arg(UpdateChecker::currentVersion(), m_release.version));
        } else {
            m_detail->setText(
                QStringLiteral("当前版本 %1，最新版本 %2。当前为便携运行，"
                               "请下载 portable 压缩包后手动解压覆盖。")
                    .arg(UpdateChecker::currentVersion(), m_release.version));
        }
        if (hasNotes)
            m_notes->setMarkdown(m_release.body);

        if (install.runningInstalled && m_release.setupAsset.isValid()) {
            m_primaryButton->setText(QStringLiteral("下载并安装"));
            m_pageButton->setText(QStringLiteral("打开发布页面"));
            m_pageButton->setVisible(true);
        } else {
            m_primaryButton->setText(QStringLiteral("前往下载页面"));
        }
        m_primaryButton->setVisible(true);
        m_secondaryButton->setText(QStringLiteral("以后再说"));
        break;

    case State::Downloading:
        m_headline->setText(QStringLiteral("正在下载 %1").arg(m_release.version));
        m_detail->setText(QStringLiteral("安装包下载完成后会自动校验 SHA256 "
                                         "并启动安装程序。"));
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        m_secondaryButton->setText(QStringLiteral("取消下载"));
        break;

    case State::Failed:
        m_headline->setText(m_errorTitle);
        m_detail->setText(m_errorMessage);
        m_detail->setStyleSheet(QStringLiteral("color: #FF6B6B;"));
        m_pageButton->setText(QStringLiteral("打开发布页面"));
        m_pageButton->setVisible(true);
        m_secondaryButton->setText(QStringLiteral("关闭"));
        break;
    }

    const QSize targetSize = m_state == State::Available ? QSize(620, 480)
                                                         : QSize(560, 260);
    if (size() != targetSize)
        resize(targetSize);
}

void UpdateDialog::showAndRaise()
{
    show();
    raise();
    activateWindow();
}

void UpdateDialog::startDownload()
{
    if (m_checker->isBusy())
        return;

    m_status->clear();
    m_progress->setValue(0);
    setState(State::Downloading);
    m_checker->downloadInstaller(m_release);
}

void UpdateDialog::cancelDownload()
{
    m_checker->abort();
    setState(State::Available);
}

void UpdateDialog::openReleasePage()
{
    const QUrl url = m_release.htmlUrl.isEmpty()
                         ? QUrl(QStringLiteral("https://github.com/AdonisZeng/Baud/releases"))
                         : QUrl(m_release.htmlUrl);
    QDesktopServices::openUrl(url);
}

void UpdateDialog::setError(const QString &title, const QString &message)
{
    m_errorTitle = title;
    m_errorMessage = message;
    setState(State::Failed);
}

void UpdateDialog::onCheckSucceeded(const UpdateRelease &release, bool updateAvailable)
{
    m_release = release;
    if (updateAvailable) {
        setState(State::Available);
        showAndRaise();
        return;
    }

    setState(State::UpToDate);
    if (m_automatic)
        hide();
    else
        showAndRaise();
}

void UpdateDialog::onCheckFailed(const QString &message)
{
    setError(QStringLiteral("检查更新失败"), message);
    if (m_automatic)
        hide();
    else
        showAndRaise();
}

void UpdateDialog::onDownloadProgress(qint64 received, qint64 total)
{
    if (m_state != State::Downloading)
        return;

    if (total > 0) {
        const int percent = static_cast<int>((received * 100) / total);
        m_progress->setRange(0, 100);
        m_progress->setValue(percent);
        m_status->setText(QStringLiteral("已下载 %1 / %2")
                              .arg(formatBytes(received), formatBytes(total)));
    } else {
        m_progress->setRange(0, 0);
        m_status->setText(QStringLiteral("已下载 %1").arg(formatBytes(received)));
    }
    m_status->setVisible(true);
}

void UpdateDialog::onDownloadSucceeded(const QString &installerPath)
{
    const UpdateChecker::InstallState install = UpdateChecker::detectInstall();
    if (!install.runningInstalled) {
        setError(QStringLiteral("无法自动更新"),
                 QStringLiteral("当前为便携运行，请手动下载并解压覆盖。"));
        showAndRaise();
        return;
    }

    QString error;
    if (!UpdateChecker::launchInstaller(installerPath, install.scope, &error)) {
        setError(QStringLiteral("无法启动安装程序"), error);
        showAndRaise();
        return;
    }

    m_primaryButton->setVisible(false);
    m_pageButton->setVisible(false);
    m_secondaryButton->setVisible(false);
    m_status->setText(QStringLiteral("安装程序已启动，Baud 即将退出以完成更新。"));
    m_status->setVisible(true);
    m_installStarted = true;
    emit installLaunched();
}

void UpdateDialog::onDownloadFailed(const QString &message)
{
    setError(QStringLiteral("下载失败"), message);
    showAndRaise();
}

void UpdateDialog::reject()
{
    if (m_state == State::Downloading && !m_installStarted) {
        // ESC / 关闭按钮：取消下载并关闭窗口；界面上的“取消下载”按钮仍回到版本页
        m_checker->abort();
        QDialog::reject();
        return;
    }
    if (m_state == State::Checking)
        m_checker->abort();
    QDialog::reject();
}
