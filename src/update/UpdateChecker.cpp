// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 AdonisZeng

#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QVersionNumber>

#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

namespace {
const QString kApiUrl = QStringLiteral(
    "https://api.github.com/repos/AdonisZeng/Baud/releases/latest");
const QString kLastCheckKey = QStringLiteral("update/lastCheckUtc");
constexpr int kAutoCheckIntervalSecs = 24 * 60 * 60;
constexpr int kApiTimeoutMs = 15000;
constexpr int kDownloadTimeoutMs = 30000;

QString normalizedDir(const QString &path)
{
    if (path.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool sameDirectory(const QString &left, const QString &right)
{
    return normalizedDir(left).compare(normalizedDir(right), Qt::CaseInsensitive) == 0;
}

QString stripVersionPrefix(QString tag)
{
    tag = tag.trimmed();
    if (tag.startsWith(QLatin1Char('v')) || tag.startsWith(QLatin1Char('V')))
        tag.remove(0, 1);
    return tag;
}

bool isSafeInstallerName(const QString &name)
{
    static const QRegularExpression pattern(
        QStringLiteral("^Baud-Setup-[A-Za-z0-9][A-Za-z0-9._-]*\\.exe$"));
    return name.size() <= 120 && !name.contains(QStringLiteral(".."))
           && pattern.match(name).hasMatch();
}

bool isGitHubAssetUrl(const QUrl &url)
{
    return url.isValid() && url.scheme() == QStringLiteral("https")
           && url.host() == QStringLiteral("github.com") && !url.fileName().isEmpty();
}

// SHA256SUMS 每行格式：<hash>  <file> 或 <hash> *<file>
QString lookupChecksum(const QString &text, const QString &fileName)
{
    const QRegularExpression separator(QStringLiteral("\\s+"));
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QStringList parts = line.trimmed().split(separator, Qt::SkipEmptyParts);
        if (parts.size() < 2)
            continue;
        QString name = parts.at(1);
        if (name.startsWith(QLatin1Char('*')))
            name.remove(0, 1);
        if (name == fileName)
            return parts.at(0).toLower();
    }
    return QString();
}

QNetworkRequest makeRequest(const QUrl &url, int timeoutMs)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent",
                         QStringLiteral("Baud/%1").arg(UpdateChecker::currentVersion()).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(timeoutMs);
    return request;
}
} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

UpdateChecker::~UpdateChecker()
{
    abort();
}

QString UpdateChecker::currentVersion()
{
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QStringLiteral("0.0.0") : version;
}

bool UpdateChecker::shouldAutoCheck()
{
    const QString stamp = QSettings().value(kLastCheckKey).toString();
    const QDateTime last = QDateTime::fromString(stamp, Qt::ISODate);
    if (!last.isValid())
        return true;
    return last.toUTC().secsTo(QDateTime::currentDateTimeUtc()) >= kAutoCheckIntervalSecs;
}

UpdateChecker::InstallState UpdateChecker::detectInstall()
{
    InstallState state;
    const QString appDir = QCoreApplication::applicationDirPath();

    struct Record {
        QString key;
        QString scope;
    };
    const Record records[] = {
        {QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\"
                        "CurrentVersion\\Uninstall\\Baud_is1"),
         QStringLiteral("user")},
        {QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\"
                        "CurrentVersion\\Uninstall\\Baud_is1"),
         QStringLiteral("machine")},
        {QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\WOW6432Node\\Microsoft\\Windows\\"
                        "CurrentVersion\\Uninstall\\Baud_is1"),
         QStringLiteral("machine")},
    };

    QString fallbackPath;
    QString fallbackScope;
    for (const Record &record : records) {
        QSettings key(record.key, QSettings::NativeFormat);
        // 卸载键位于 HKCU 或 HKLM 即表示当前用户或所有用户安装
        QString path = key.value(QStringLiteral("InstallLocation")).toString();
        if (path.isEmpty())
            path = key.value(QStringLiteral("Inno Setup: App Path")).toString();
        if (path.isEmpty())
            continue;

        state.installed = true;
        if (fallbackPath.isEmpty()) {
            fallbackPath = path;
            fallbackScope = record.scope;
        }
        if (sameDirectory(path, appDir)) {
            state.installPath = path;
            state.scope = record.scope;
            state.runningInstalled = true;
            return state;
        }
    }

    if (state.installed) {
        state.installPath = fallbackPath;
        state.scope = fallbackScope;
    }
    return state;
}

bool UpdateChecker::launchInstaller(const QString &installerPath, const QString &scope,
                                    QString *errorMessage)
{
    const QString arguments =
        QStringLiteral("/SILENT /NORESTART /CLOSEAPPLICATIONS /%1")
            .arg(scope == QStringLiteral("machine") ? QStringLiteral("ALLUSERS")
                                                    : QStringLiteral("CURRENTUSER"));
    const QString nativePath = QDir::toNativeSeparators(installerPath);
    const std::wstring file = nativePath.toStdWString();
    const std::wstring params = arguments.toStdWString();
    const wchar_t *verb = scope == QStringLiteral("machine") ? L"runas" : L"open";

    const HINSTANCE result =
        ShellExecuteW(nullptr, verb, file.c_str(), params.c_str(), nullptr, SW_SHOWNORMAL);
    const qintptr code = reinterpret_cast<qintptr>(result);
    if (code > 32)
        return true;

    if (errorMessage) {
        if (code == SE_ERR_ACCESSDENIED)
            *errorMessage = QStringLiteral("已取消管理员授权，无法完成机器级安装更新。");
        else
            *errorMessage = QStringLiteral("无法启动安装程序（ShellExecute 错误码 %1）。")
                                .arg(code);
    }
    return false;
}

bool UpdateChecker::isBusy() const
{
    return m_checkReply != nullptr || m_downloadReply != nullptr;
}

void UpdateChecker::checkForUpdates()
{
    if (isBusy())
        return;

    m_cancelled = false;

    QNetworkRequest request = makeRequest(QUrl(kApiUrl), kApiTimeoutMs);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");

    m_checkReply = m_network->get(request);
    connect(m_checkReply, &QNetworkReply::finished,
            this, &UpdateChecker::handleCheckFinished);
}

void UpdateChecker::handleCheckFinished()
{
    QNetworkReply *reply = m_checkReply;
    m_checkReply = nullptr;
    if (!reply)
        return;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString remaining =
        QString::fromLatin1(reply->rawHeader("X-RateLimit-Remaining"));
    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    reply->deleteLater();

    if (m_cancelled) {
        m_cancelled = false;
        return;
    }

    // 无论成功失败都记录本次检查时间，避免失败后每次启动都触发 API 请求
    markChecked();

    if (status == 404) {
        failCheck(QStringLiteral("GitHub 仓库还没有发布任何版本。"));
        return;
    }
    if (status == 403) {
        if (remaining == QStringLiteral("0"))
            failCheck(QStringLiteral("GitHub API 请求次数已超限，请稍后重试，"
                                     "或打开发布页面手动下载。"));
        else
            failCheck(QStringLiteral("GitHub 拒绝了请求（HTTP 403）。"));
        return;
    }
    if (error != QNetworkReply::NoError) {
        failCheck(QStringLiteral("无法访问 GitHub：%1").arg(errorText));
        return;
    }
    if (status != 200) {
        failCheck(QStringLiteral("GitHub 返回异常状态码 %1。").arg(status));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        failCheck(QStringLiteral("GitHub 返回的数据无法解析。"));
        return;
    }

    const QJsonObject object = document.object();
    UpdateRelease release;
    release.tagName = object.value(QStringLiteral("tag_name")).toString();
    release.version = stripVersionPrefix(release.tagName);
    release.name = object.value(QStringLiteral("name")).toString();
    release.body = object.value(QStringLiteral("body")).toString();
    release.htmlUrl = object.value(QStringLiteral("html_url")).toString();

    const QJsonArray assets = object.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        // 资产名必须与下载 URL 的文件名一致，且只接受 github.com 的 HTTPS 地址
        if (!isGitHubAssetUrl(url) || url.fileName() != name)
            continue;
        if (isSafeInstallerName(name)) {
            release.setupAsset = url;
            release.setupAssetName = name;
        } else if (name == QStringLiteral("SHA256SUMS.txt")
                   || name == QStringLiteral("SHA256SUMS")) {
            release.checksumAsset = url;
            release.checksumAssetName = name;
        }
    }

    if (!release.isValid()) {
        failCheck(QStringLiteral("最新发布中缺少有效的版本号。"));
        return;
    }

    const QVersionNumber latest = QVersionNumber::fromString(release.version);
    if (latest.isNull()) {
        failCheck(QStringLiteral("无法识别版本号 %1。").arg(release.version));
        return;
    }
    const QVersionNumber current = QVersionNumber::fromString(currentVersion());
    const bool updateAvailable = latest > current;

    emit checkSucceeded(release, updateAvailable);
}

void UpdateChecker::downloadInstaller(const UpdateRelease &release)
{
    if (isBusy())
        return;

    m_pendingRelease = release;
    m_cancelled = false;
    m_expectedHash.clear();

    if (!release.setupAsset.isValid() || !isSafeInstallerName(release.setupAssetName)) {
        emit downloadFailed(QStringLiteral("该版本没有提供有效的 Windows 安装包，"
                                           "请打开发布页面手动下载。"));
        return;
    }
    if (!release.checksumAsset.isValid() || release.checksumAssetName.isEmpty()) {
        emit downloadFailed(QStringLiteral("该发布缺少 SHA256SUMS.txt，"
                                           "出于安全考虑已停止自动下载，"
                                           "请打开发布页面手动下载。"));
        return;
    }

    QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (directory.isEmpty())
        directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    directory += QStringLiteral("/updates");
    if (!QDir().mkpath(directory)) {
        emit downloadFailed(QStringLiteral("无法创建下载目录：%1").arg(directory));
        return;
    }

    QString fileName = release.setupAssetName;
    if (!isSafeInstallerName(fileName)) {
        emit downloadFailed(QStringLiteral("安装包文件名不合法，已停止自动下载。"));
        return;
    }
    m_targetPath = directory + QLatin1Char('/') + fileName;

    startChecksumDownload();
}

void UpdateChecker::startChecksumDownload()
{
    QNetworkRequest request = makeRequest(m_pendingRelease.checksumAsset, kApiTimeoutMs);
    m_downloadReply = m_network->get(request);
    connect(m_downloadReply, &QNetworkReply::finished,
            this, &UpdateChecker::handleChecksumFinished);
}

void UpdateChecker::handleChecksumFinished()
{
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    if (!reply)
        return;

    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (m_cancelled) {
        m_cancelled = false;
        cleanupDownload();
        return;
    }
    if (error != QNetworkReply::NoError) {
        failDownload(QStringLiteral("下载校验文件失败：%1").arg(errorText));
        return;
    }

    const QString fileName = QFileInfo(m_targetPath).fileName();
    m_expectedHash = lookupChecksum(QString::fromUtf8(payload), fileName);
    if (m_expectedHash.isEmpty()) {
        failDownload(QStringLiteral("校验文件中没有 %1 的 SHA256 值。").arg(fileName));
        return;
    }

    startInstallerDownload();
}

void UpdateChecker::startInstallerDownload()
{
    QNetworkRequest request = makeRequest(m_pendingRelease.setupAsset, kDownloadTimeoutMs);
    request.setRawHeader("Accept", "application/octet-stream");

    m_downloadReply = m_network->get(request);
    connect(m_downloadReply, &QNetworkReply::downloadProgress,
            this, &UpdateChecker::downloadProgress);
    connect(m_downloadReply, &QNetworkReply::finished,
            this, &UpdateChecker::handleDownloadFinished);
}

void UpdateChecker::handleDownloadFinished()
{
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    if (!reply)
        return;

    const QNetworkReply::NetworkError error = reply->error();
    const QByteArray data = reply->readAll();

    if (m_cancelled) {
        m_cancelled = false;
        reply->deleteLater();
        cleanupDownload();
        return;
    }
    if (error != QNetworkReply::NoError) {
        const QString message = downloadErrorText(reply);
        reply->deleteLater();
        failDownload(message);
        return;
    }
    reply->deleteLater();

    const QByteArray digest =
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    if (digest.compare(m_expectedHash.toLatin1(), Qt::CaseInsensitive) != 0) {
        cleanupDownload();
        emit downloadFailed(QStringLiteral("安装包 SHA256 校验失败，下载文件已删除。"));
        return;
    }

    const QString partPath = m_targetPath + QStringLiteral(".part");
    QFile file(partPath);
    if (!file.open(QIODevice::WriteOnly)) {
        failDownload(QStringLiteral("无法写入下载目录：%1").arg(QFileInfo(partPath).absolutePath()));
        return;
    }
    const bool written = file.write(data) == data.size();
    file.close();
    if (!written) {
        failDownload(QStringLiteral("写入安装包失败，请检查磁盘空间。"));
        return;
    }

    QFile::remove(m_targetPath);
    if (!QFile::rename(partPath, m_targetPath)) {
        QFile::remove(partPath);
        emit downloadFailed(QStringLiteral("无法保存安装包到：%1").arg(m_targetPath));
        return;
    }

    emit downloadSucceeded(m_targetPath);
}

QString UpdateChecker::downloadErrorText(QNetworkReply *reply) const
{
    switch (reply->error()) {
    case QNetworkReply::HostNotFoundError:
        return QStringLiteral("下载失败：无法解析 GitHub 下载地址，请检查网络连接。");
    case QNetworkReply::TimeoutError:
        return QStringLiteral("下载失败：连接超时，请稍后重试。");
    case QNetworkReply::SslHandshakeFailedError:
        return QStringLiteral("下载失败：TLS 握手失败，请检查系统时间或代理设置。");
    case QNetworkReply::OperationCanceledError:
        return QStringLiteral("下载已取消。");
    default:
        return QStringLiteral("下载失败：%1").arg(reply->errorString());
    }
}

void UpdateChecker::abort()
{
    m_cancelled = true;
    if (m_checkReply)
        m_checkReply->abort();
    if (m_downloadReply)
        m_downloadReply->abort();
}

void UpdateChecker::failCheck(const QString &message)
{
    emit checkFailed(message);
}

void UpdateChecker::failDownload(const QString &message)
{
    cleanupDownload();
    emit downloadFailed(message);
}

void UpdateChecker::markChecked()
{
    QSettings().setValue(kLastCheckKey,
                         QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
}

void UpdateChecker::cleanupDownload()
{
    if (!m_targetPath.isEmpty())
        QFile::remove(m_targetPath + QStringLiteral(".part"));
    m_expectedHash.clear();
}
