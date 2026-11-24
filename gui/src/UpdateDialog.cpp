#include "UpdateDialog.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVersionNumber>
#include <QVBoxLayout>
#include <stdexcept>

UpdateInfo parseUpdate(const QByteArray& data, const QString& currentVersion) {
    if (data.size() > 1024 * 1024) throw std::runtime_error("Update response exceeds limit");
    const auto document = QJsonDocument::fromJson(data);
    if (!document.isObject()) throw std::runtime_error("Invalid update response");
    const auto release = document.object();
    if (release.value("draft").toBool(true) || release.value("prerelease").toBool(true))
        throw std::runtime_error("No stable update available");
    static const QRegularExpression pattern("^v([0-9]+\\.[0-9]+\\.[0-9]+)$");
    const auto tag = release.value("tag_name").toString();
    const auto match = pattern.match(tag);
    if (!match.hasMatch()) throw std::runtime_error("Invalid update version");
    const QString version = match.captured(1);
    qsizetype consumed = 0;
    const auto parsed = QVersionNumber::fromString(version, &consumed);
    if (consumed != version.size() || parsed.segmentCount() != 3) throw std::runtime_error("Invalid update version");
    const auto current = QVersionNumber::fromString(currentVersion);
    if (current.segmentCount() != 3) throw std::runtime_error("Invalid current version");
    const QUrl expected("https://github.com/liuyb4016/ggZip/releases/tag/" + tag);
    if (QUrl(release.value("html_url").toString()) != expected) throw std::runtime_error("Unexpected update origin");
    UpdateInfo result;
    result.version = version;
    result.notes = release.value("body").toString();
    result.releaseUrl = expected;
    result.newer = QVersionNumber::compare(parsed, current) > 0;
    return result;
}

UpdateDialog::UpdateDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("检查更新"));
    resize(680, 460);
    status_ = new QLabel(tr("正在查询官方 GitHub 发布…"));
    notes_ = new QPlainTextEdit;
    notes_->setReadOnly(true);
    open_ = new QPushButton(tr("打开官方发布页/下载"));
    open_->setEnabled(false);
    connect(open_, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(releaseUrl_); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("只查询版本并显示说明；不会下载、安装或执行更新程序。")));
    layout->addWidget(status_);
    layout->addWidget(notes_);
    layout->addWidget(open_);
    layout->addWidget(buttons);
    network_ = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl("https://api.github.com/repos/liuyb4016/ggZip/releases/latest"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "ggZip/" GGZIP_VERSION);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    reply_ = network_->get(request);
    connect(this, &QDialog::finished, reply_, &QNetworkReply::abort);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (reply_->bytesAvailable() > 1024 * 1024 - received_.size()) {
            status_->setText(tr("更新响应超过限制。"));
            reply_->abort();
            return;
        }
        received_.append(reply_->readAll());
    });
    connect(reply_, &QNetworkReply::finished, this, [this] {
        if (reply_->error() != QNetworkReply::NoError ||
            reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            status_->setText(tr("无法查询稳定版本；网络失败不影响正常使用。"));
            return;
        }
        try {
            const auto release = parseUpdate(received_, GGZIP_VERSION);
            releaseUrl_ = release.releaseUrl;
            notes_->setPlainText(release.notes);
            status_->setText(release.newer ? tr("发现新版本：%1").arg(release.version) :
                tr("当前版本 %1，无更新的稳定版本。").arg(GGZIP_VERSION));
            open_->setEnabled(true);
        } catch (const std::exception&) { status_->setText(tr("发布信息不符合安全要求，未提供更新入口。")); }
    });
    QTimer::singleShot(30000, reply_, &QNetworkReply::abort);
}
